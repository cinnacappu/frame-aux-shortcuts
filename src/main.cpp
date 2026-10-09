// frame-aux-shortcuts: Steam Frame 本体の aux ボタン（電源ボタンの上）の長押し・1〜5 回押しに、アクションを割り当てる常駐アプリ。
//
// 仕組み: gpio-keys のキー対応表で aux（KEY_SELECT 353）を KEY_MACRO1（656）に置き換えて SteamVR に届かなくし、
// evdev を grab せずに読んで押し方を判定する。終わるとき（正常終了・SIGTERM・SIGINT・SIGHUP・SIGUSR2・SteamVR の終了・
// 致命的なシグナル）には必ず 656 → 353 に戻す。SIGKILL では戻らないので、そのときは --restore か再起動で戻す。
// 設定の run_in_background が false なら、設定パネル（frame-aux-shortcuts-panel）が動いている間だけ置き換える。
// 状態（最後の押し方・ミュート・明るさなど）は $XDG_RUNTIME_DIR/frame-aux-shortcuts/status.json に書き、設定パネルが読む。
// sudo は使わない（steamos は input グループなので /dev/input/event* を読み書きできる）。
#include "action_worker.h"
#include "actions.h"
#include "brightness.h"
#include "command.h"
#include "config.h"
#include "draw.h"
#include "hud_message.h"
#include "hud_overlay.h"
#include "hud_render.h"
#include "i18n.h"
#include "keymap.h"
#include "mic_monitor.h"
#include "osc.h"
#include "press_detector.h"
#include "runtime.h"
#include "sound.h"
#include "status.h"
#include "theme.h"
#include "vr_client.h"

#include <fcntl.h>
#include <linux/input.h>
#include <poll.h>
#include <signal.h>
#include <sys/file.h>
#include <sys/ioctl.h>
#include <unistd.h>

#include <algorithm>
#include <cerrno>
#include <memory>
#include <cmath>
#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <string>
#include <vector>

// バージョンは CMakeLists.txt の project(VERSION) から入る
#ifndef FRAME_AUX_SHORTCUTS_VERSION
#define FRAME_AUX_SHORTCUTS_VERSION "unknown"
#endif

namespace {

/** 止める知らせ（SIGTERM・SIGINT・SIGHUP・SIGUSR2）を受けたら、そのシグナルの番号が入る。 */
volatile std::sig_atomic_t gStopSignal = 0;

/** 終了コード: 引数の誤り。 */
constexpr int kExitCodeUsage = 2;
/**
 * 終了コード: 起動し直しても動けない（すでに常駐している・gpio-keys や aux ボタンが無い・開けない）か、
 * 設定パネルの「終了」（SIGUSR2）で終わった。systemd の RestartPreventExitStatus= と SuccessExitStatus= に書く。
 */
constexpr int kExitCodeNoRestart = 3;

constexpr int64_t kVrRetryMs = 3000;      ///< SteamVR につながらないとき、もう一度試す間隔
constexpr int64_t kVrPollMs = 250;        ///< SteamVR のイベント（終了の知らせ・room view）を読む間隔
constexpr int64_t kVrShotPollMs = 30;     ///< スクリーンショットの結果を待っている間、SteamVR のイベントを読む間隔
constexpr int64_t kConfigCheckMs = 1000;  ///< 設定ファイルの変更を確かめる間隔
constexpr int64_t kPanelCheckMs = 500;    ///< 設定パネルが動いているかを確かめる間隔（run_in_background が false のとき）
constexpr int64_t kSampleMs = 1000;       ///< 明るさ・パススルーを読む間隔
constexpr int64_t kAppsCheckMs = 2000;    ///< frame-perf-overlay の状態を読む間隔
constexpr int kMicFastMs = 1000;          ///< ミュートを読む間隔（ミュート中の表示か設定パネルが要るとき）
constexpr int kMicSlowMs = 5000;          ///< ミュートを読む間隔（それ以外）

/** コマンドラインの内容。 */
struct Options {
    enum class Mode { Run, Restore, ShowKeymap, TestAction, SendOsc, DumpHud, Help, Version };
    Mode mode = Mode::Run;
    std::string configPath;
    std::string testAction;  ///< --test-action の種類の名前
    std::string oscTarget;   ///< --send-osc の送り先（auto:9000 / <IPv4>:<ポート>）
    std::string oscAddress;  ///< --send-osc の OSC のアドレス
    std::string dumpDir;     ///< --dump-hud-png の書き出し先
    bool shotKeepBadge = false;  ///< --shot-keep-badge: 確かめ用。--test-action screenshot でミュート中の表示を隠さずに撮る
    bool noVr = false;       ///< --no-vr: SteamVR につながない（確かめ用）
};

/**
 * 止める知らせを受けたら、止める印をつける。
 * @param signal 受けたシグナル
 */
void onStopSignal(int signal) {
    gStopSignal = signal;
}

/** 止める知らせのハンドラーを入れ、SIGPIPE は無視する（room view の送信で相手が閉じていても落ちない）。 */
void installSignalHandlers() {
    struct sigaction action {};
    action.sa_handler = onStopSignal;
    sigemptyset(&action.sa_mask);
    for (const int s : {SIGTERM, SIGINT, SIGHUP, SIGUSR2}) sigaction(s, &action, nullptr);
    signal(SIGPIPE, SIG_IGN);
}

/**
 * 単調増加の時計で今の時刻を ms で返す。
 * @return ms
 */
int64_t nowMs() {
    struct timespec ts {};
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return static_cast<int64_t>(ts.tv_sec) * 1000 + ts.tv_nsec / 1000000;
}

/** 使い方を表示する。 */
void printUsage() {
    std::printf(
        "使い方: frame-aux-shortcuts [オプション]\n"
        "  （なし）             常駐する。aux ボタンを置き換えて、押し方ごとのアクションを動かす\n"
        "  --restore            キー対応表を元に戻す（656 → 353）だけをして終わる\n"
        "  --show-keymap        キー対応表を読んで表示するだけで終わる（書き換えない）\n"
        "  --test-action TYPE   アクションを 1 回だけ実行して終わる（キー対応表は触らない）\n"
        "                       TYPE: mic_mute_toggle / brightness_cycle / room_view_toggle / screenshot /\n"
        "                             perf_overlay_toggle / dashboard_open / command / none\n"
        "                       （command は設定の割り当てのうち最初のもののコマンドを使う）\n"
        "  --send-osc TARGET ADDRESS  OSC の ADDRESS に 1 を送り、0.15 秒後に 0 を送って終わる（ボタンを押して離す）。\n"
        "                       TARGET: auto:9000（Steam Link でつないでいる PC）か <IPv4>:<ポート>。\n"
        "                       例: --send-osc auto:9000 /input/Voice（VRChat のミュートの切り替え）\n"
        "  --dump-hud-png DIR   OpenVR なしで、視界の表示（お知らせ・ミュート中の表示）を日英で PNG に書き出して終わる\n"
        "  --no-vr              確かめ用: SteamVR につながない（視界の表示も出さない）\n"
        "  --shot-keep-badge    確かめ用: --test-action screenshot で、ミュート中の表示を隠さずに撮る（写るかを比べる）\n"
        "  --config PATH        設定ファイル（既定 ~/.config/frame-aux-shortcuts/config.json）\n"
        "  --version            バージョンを表示して終わる\n");
}

/**
 * コマンドラインを読む。
 * @param argc 引数の数
 * @param argv 引数
 * @param options 書き込み先
 * @return 正しく読めたら true
 */
bool parseOptions(int argc, char** argv, Options& options) {
    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        const bool hasNext = i + 1 < argc;
        if (arg == "--restore") {
            options.mode = Options::Mode::Restore;
        } else if (arg == "--show-keymap") {
            options.mode = Options::Mode::ShowKeymap;
        } else if (arg == "--test-action" && hasNext) {
            options.mode = Options::Mode::TestAction;
            options.testAction = argv[++i];
        } else if (arg == kSendOscFlag && i + 2 < argc) {
            options.mode = Options::Mode::SendOsc;
            options.oscTarget = argv[++i];
            options.oscAddress = argv[++i];
        } else if (arg == "--dump-hud-png" && hasNext) {
            options.mode = Options::Mode::DumpHud;
            options.dumpDir = argv[++i];
        } else if (arg == "--shot-keep-badge") {
            options.shotKeepBadge = true;
        } else if (arg == "--no-vr") {
            options.noVr = true;
        } else if (arg == "--config" && hasNext) {
            options.configPath = argv[++i];
        } else if (arg == "--version") {
            options.mode = Options::Mode::Version;
        } else if (arg == "--help" || arg == "-h") {
            options.mode = Options::Mode::Help;
        } else {
            std::fprintf(stderr, "知らない引数です: %s\n", arg.c_str());
            return false;
        }
    }
    if (options.configPath.empty()) options.configPath = defaultConfigPath();
    return true;
}

/**
 * 常駐のロックを取る。取れたら自分の PID を書いて、ファイルを開いたままにする（終われば OS がロックを外す）。
 * 2 つ同時に動くと、片方が終わるときに対応表を戻してしまうので、1 つだけにする。
 * @param lockFd 取れたときのファイル（開いたままにする）の書き込み先
 * @param holderPid 取れなかったときの、常駐側の PID の書き込み先（読めなければ 0）
 * @return ロックを取れた（またはロックを使えないので、そのまま起動してよい）なら true
 */
bool acquireInstanceLock(int& lockFd, pid_t& holderPid) {
    lockFd = -1;
    holderPid = 0;
    std::string reason;
    const int fd = openLockFile(kDaemonName, reason);
    if (fd < 0) {
        std::fprintf(stderr, "[起動] ロックを取れません（%s）。二重起動の確認をせずに起動します\n", reason.c_str());
        return true;
    }
    if (::flock(fd, LOCK_EX | LOCK_NB) == 0) {
        const std::string pid = std::to_string(::getpid()) + "\n";
        if (::ftruncate(fd, 0) != 0 || ::pwrite(fd, pid.data(), pid.size(), 0) < 0) {
            std::fprintf(stderr, "[起動] ロックファイルに PID を書けませんでした\n");
        }
        lockFd = fd;
        return true;
    }
    char buffer[32] = {};
    if (::pread(fd, buffer, sizeof(buffer) - 1, 0) > 0) holderPid = static_cast<pid_t>(std::atol(buffer));
    ::close(fd);
    return false;
}

/**
 * --show-keymap: 対応表を読んで表示する（書き換えない）。
 * @return 終了コード
 */
int runShowKeymap() {
    const std::string device = findGpioKeysDevice();
    if (device.empty()) {
        std::fprintf(stderr, "gpio-keys が見つかりません\n");
        return 1;
    }
    const int fd = ::open(device.c_str(), O_RDONLY | O_CLOEXEC);
    if (fd < 0) {
        std::fprintf(stderr, "%s を開けません: %s\n", device.c_str(), std::strerror(errno));
        return 1;
    }
    std::printf("%s %s\n", device.c_str(), formatKeymap(readKeymap(fd)).c_str());
    ::close(fd);
    return 0;
}

/**
 * --restore: 対応表を元に戻す（656 → 353）だけをする。
 * @return 終了コード
 */
int runRestore() {
    int lockFd = -1;
    pid_t holderPid = 0;
    if (!acquireInstanceLock(lockFd, holderPid)) {
        std::fprintf(stderr, "[戻す] 常駐しているもの（PID %d）がいます。戻すと、そちらは押し方を読めなくなります\n",
                     static_cast<int>(holderPid));
    }
    if (lockFd >= 0) ::close(lockFd);  // 確かめただけなので、すぐ離す
    const std::string device = findGpioKeysDevice();
    if (device.empty()) {
        std::fprintf(stderr, "[戻す] gpio-keys が見つかりません\n");
        return 1;
    }
    const int fd = ::open(device.c_str(), O_RDWR | O_CLOEXEC);
    if (fd < 0) {
        std::fprintf(stderr, "[戻す] %s を開けません: %s\n", device.c_str(), std::strerror(errno));
        return 1;
    }
    const std::string before = formatKeymap(readKeymap(fd));
    const int restored = swapKeycode(fd, kAuxStandInCode, kAuxOriginalCode);
    const std::string after = formatKeymap(readKeymap(fd));
    ::close(fd);
    if (restored < 0) {
        std::fprintf(stderr, "[戻す] %s の対応表を書き換えられません: %s\n", device.c_str(), std::strerror(errno));
        return 1;
    }
    std::printf("%s %s → %s（%d 項目を戻しました）\n", device.c_str(), before.c_str(), after.c_str(), restored);
    return 0;
}

/**
 * Config から音の設定を取り出す。
 * @param config 設定
 * @return 音の設定
 */
SoundSettings soundSettingsOf(const Config& config) {
    SoundSettings sound;
    sound.enabled = config.soundEnabled;
    sound.success = config.successSound;
    sound.failure = config.failureSound;
    return sound;
}

/**
 * --test-action: アクションを 1 回だけ実行する（キー対応表は触らない）。
 * @param options コマンドライン
 * @return 成功なら 0、失敗なら 1、種類が分からなければ 2
 */
int runTestAction(const Options& options) {
    ActionType type = ActionType::None;
    if (!parseActionType(options.testAction, type)) {
        std::fprintf(stderr, "知らないアクションの種類です: %s\n", options.testAction.c_str());
        return kExitCodeUsage;
    }
    ConfigWatcher watcher(options.configPath);
    const Config config = watcher.loadInitial();
    Action action;
    action.type = type;
    for (int slot = 0; slot < kSlotCount; ++slot) {
        if (config.slot(slot).type == type) {
            action = config.slot(slot);
            break;
        }
    }
    if (action.type == ActionType::Command && action.commandId.empty()) {
        std::fprintf(stderr, "[試し] command を割り当てた押し方がありません\n");
        return 1;
    }

    VrClient vr;
    if (actionNeedsVr(type)) {
        std::string message;
        if (vr.connect(message) != VrClient::ConnectResult::Ok) {
            std::fprintf(stderr, "[VR] 接続できません: %s\n", message.c_str());
        }
    }
    // スクリーンショット: 常駐と同じく、ミュート中の表示を出していれば隠して待ってから頼み、終わったら出し直す。
    // --test-action は別のプロセスなので、ミュート中ならこのプロセスでも表示を出して（常駐と同じ見え方にして）から試す
    FontSet fonts;
    std::unique_ptr<HudRenderer> renderer;
    HudOverlay overlay(".test." + std::to_string(::getpid()));
    bool badge = false;
    if (type == ActionType::Screenshot && vr.connected() && config.muteBadge) {
        const CommandResult volume = runCommand({"wpctl", "get-volume", "@DEFAULT_AUDIO_SOURCE@"});
        bool muted = false;
        if (volume.ok() && parseWpctlMuted(volume.out, muted) && muted) {
            fonts.load(kFontRegular, kFontBold);
            renderer = std::make_unique<HudRenderer>(fonts);
            badge = overlay.setBadge(&renderer->renderBadge(config.language));
            std::fprintf(stderr, "[試し] ミュート中なので、ミュート中の表示を%s\n",
                         !badge ? "出せませんでした" : (options.shotKeepBadge ? "出しました（隠さずに撮ります）" : "出しました（撮る前に隠します）"));
            ::usleep(1000 * 1000);  // 表示が出るのを待つ
            if (badge && !options.shotKeepBadge) {
                overlay.setBadge(nullptr);
                ::usleep(kBadgeHideWaitMs * 1000);
            }
        }
    }
    PreparedAction prepared = prepareAction(action, config, vr);
    ActionOutcome outcome;
    if (prepared.screenshot) {
        // 結果は SteamVR のイベント（VREvent_ScreenshotTaken / Failed）で来る。5 秒まで待つ
        const int64_t deadline = nowMs() + static_cast<int64_t>(kScreenshotTimeoutSec * 1000);
        bool ok = false;
        bool arrived = false;
        while (!arrived && nowMs() < deadline && !gStopSignal) {
            vr.pollEvents();
            arrived = vr.screenshotResult(prepared.shot.handle, ok);
            if (!arrived) ::usleep(50 * 1000);
        }
        outcome = arrived ? finishScreenshot(vr, prepared.shot, ok, config.screenshotToSteam)
                          : screenshotTimedOut(prepared.shot);
    } else {
        outcome = prepared.done ? prepared.outcome : prepared.deferred();
    }
    if (badge) {
        overlay.setBadge(&renderer->renderBadge(config.language));  // 出し直す（すぐ終わるので見えるのは一瞬）
        overlay.releaseOverlays();
    }
    std::fprintf(stderr, "[試し] %s: %s %s\n", actionTypeName(type), outcome.ok ? "成功" : "失敗", outcome.detail.c_str());
    if (type != ActionType::None) playFeedbackSound(soundSettingsOf(config), outcome.ok);
    vr.shutdown();
    overlay.releaseVulkan();
    return outcome.ok ? 0 : 1;
}

/**
 * --send-osc: OSC を 1 回だけ送る（設定パネルで登録したプリセットと同じ動き。キー対応表は触らない）。
 * 出力には送り先の IP を出さない（FRAME_AUX_DEBUG=1 のときだけ出す）。
 * @param options コマンドライン
 * @return 送れたら 0、送れなければ 1、書き方が違えば 2
 */
int runSendOsc(const Options& options) {
    SendOscCommand command;
    if (!parseSendOscCommand({kSendOscProgram, kSendOscFlag, options.oscTarget, options.oscAddress}, command)) {
        std::fprintf(stderr, "送り先（auto:9000 / <IPv4>:<ポート>）かアドレス（/ で始まる）の書き方が違います\n");
        return kExitCodeUsage;
    }
    const SendOscResult result = sendOscPress(command);
    if (!result.ok) {
        std::fprintf(stderr, "[OSC] %s を送れません: %s\n", command.address.c_str(), result.error.c_str());
        return 1;
    }
    std::fprintf(stderr, "[OSC] %s に 1 → 0 を送りました%s\n", command.address.c_str(),
                 commandDebugEnabled() ? (" ［" + result.host + ":" + std::to_string(command.port) + "］").c_str() : "");
    return 0;
}

/**
 * --dump-hud-png: 視界の表示を日英・主な種類で PNG に書き出す（OpenVR なし）。
 * @param options コマンドライン
 * @return 終了コード
 */
int runDumpHud(const Options& options) {
    FontSet fonts;
    fonts.load(kFontRegular, kFontBold);
    HudRenderer renderer(fonts);
    struct Sample {
        const char* name;
        int count;  ///< 0 なら長押し
        ActionType action;
        bool ok;
        const char* value;
        const char* reason;
    };
    const Sample samples[] = {
        {"mic-muted", 1, ActionType::MicMuteToggle, true, "muted", ""},
        {"mic-on", 1, ActionType::MicMuteToggle, true, "unmuted", ""},
        {"brightness", 2, ActionType::BrightnessCycle, true, "60", ""},
        {"passthrough-on", 3, ActionType::RoomViewToggle, true, "on", ""},
        {"passthrough-failed", 3, ActionType::RoomViewToggle, false, "on", "room_view_api"},
        {"screenshot", 4, ActionType::Screenshot, true, "/tmp/x.png", ""},
        {"screenshot-failed", 4, ActionType::Screenshot, false, "", "failed"},
        {"perf-shown", 5, ActionType::PerfOverlayToggle, true, "shown", ""},
        {"perf-missing", 5, ActionType::PerfOverlayToggle, false, "", "app_missing"},
        {"dashboard-failed", 0, ActionType::DashboardOpen, false, "", "no_steamvr"},
        {"vrchat-mute", 2, ActionType::Command, true, "osc:/input/Voice", ""},
        {"vrchat-mute-nopc", 2, ActionType::Command, false, "osc:/input/Voice", "no_pc"},
    };
    int written = 0;
    for (const Language lang : {Language::Ja, Language::En}) {
        const std::string suffix = lang == Language::Ja ? "ja" : "en";
        for (const Sample& sample : samples) {
            PressRecord press;
            press.kind = sample.count == 0 ? PressRecord::Kind::Long : PressRecord::Kind::Count;
            press.count = sample.count;
            press.action = sample.action;
            press.ok = sample.ok;
            press.value = sample.value;
            press.reason = sample.reason;
            HudToast toast;
            if (!makeHudToast(press, lang, toast)) continue;
            renderer.renderToast(toast);
            const std::string path = options.dumpDir + "/hud-" + suffix + "-" + sample.name + ".png";
            if (!renderer.writeToastPng(path)) {
                std::fprintf(stderr, "PNG を書き出せませんでした: %s\n", path.c_str());
                return 1;
            }
            ++written;
        }
        renderer.renderBadge(lang);
        const std::string badgePath = options.dumpDir + "/hud-" + suffix + "-mute-badge.png";
        if (!renderer.writeBadgePng(badgePath)) {
            std::fprintf(stderr, "PNG を書き出せませんでした: %s\n", badgePath.c_str());
            return 1;
        }
        ++written;
    }
    std::printf("視界の表示を %d 枚書き出しました: %s\n", written, options.dumpDir.c_str());
    return 0;
}

/**
 * 設定の時間を判定に渡す形にする。
 * @param config 設定
 * @return 判定の時間と回数
 */
PressDetector::Params detectorParams(const Config& config) {
    PressDetector::Params params;
    params.longPressMs = config.longPressMs;
    params.multiPressGapMs = config.multiPressGapMs;
    params.maxCount = kMaxPressCount;
    return params;
}

/**
 * 常駐の中身（押し方の判定・アクション・視界の表示・状態ファイル）。
 */
class Daemon {
public:
    /**
     * @param config 最初の設定
     * @param wakeFd 作業用スレッド・ミュートの見張りが書くパイプ
     */
    Daemon(const Config& config, int wakeFd) : config_(config), detector_(detectorParams(config)), hud_(fonts_) {
        fonts_.load(kFontRegular, kFontBold);
        status_.version = FRAME_AUX_SHORTCUTS_VERSION;
        status_.pid = static_cast<int>(::getpid());
        worker_.start(wakeFd);
        mic_.start(wakeFd);
    }

    /** 片付ける（作業用スレッド・見張りを止め、視界の表示を消して SteamVR から離れる）。 */
    void shutdown() {
        worker_.stop();
        mic_.stop();
        overlay_.releaseOverlays();
        vr_.shutdown();
        overlay_.releaseVulkan();
        status_.active = false;
        status_.vrConnected = false;
        writeStatus(true);
    }

    /** @return 押し方の判定 */
    PressDetector& detector() { return detector_; }
    /** @return 今の設定 */
    Config& config() { return config_; }
    /** @return SteamVR とのつながり */
    VrClient& vr() { return vr_; }

    /**
     * aux を置き換えているか（ショートカットが効くか）を状態に入れる。
     * @param active 置き換えていれば true
     */
    void setActive(bool active) {
        status_.active = active;
        if (!active) detector_.reset();
        updateBadge();
        writeStatus();
    }

    /**
     * 設定パネルが動いているかを知らせる（動いている間はミュートを細かく読む）。
     * @param running 動いていれば true
     */
    void setPanelRunning(bool running) { panelRunning_ = running; }

    /** 設定が変わったときに呼ぶ（判定の時間・ミュート中の表示の言語やオン・オフ）。 */
    void onConfigChanged() {
        detector_.setParams(detectorParams(config_));
        badgeLanguage_ = -1;  // 言語が変わっていれば描き直す
        updateBadge();
    }

    /**
     * 確定した押し方に割り当てたアクションを動かす。
     * @param result 確定した押し方
     */
    void handlePress(const PressResult& result) {
        if (!result.decided()) return;
        PressRecord record;
        record.at = static_cast<long long>(std::time(nullptr));
        record.count = result.count;
        if (result.kind == PressResult::Kind::TooMany) {
            std::fprintf(stderr, "[押し方] %d 回押し: %d 回を超えたので何もしません\n", result.count, kMaxPressCount);
            record.kind = PressRecord::Kind::TooMany;
            status_.lastPress = record;
            writeStatus();
            return;
        }
        const bool longPress = result.kind == PressResult::Kind::LongPress;
        record.kind = longPress ? PressRecord::Kind::Long : PressRecord::Kind::Count;
        const Action& action = longPress ? config_.longPress : config_.press[result.count - 1];
        record.action = action.type;
        const std::string label = (longPress ? std::string("長押し") : std::to_string(result.count) + " 回押し") +
                                  " → " + actionTypeName(action.type);
        if (action.type == ActionType::None) {
            std::fprintf(stderr, "[押し方] %s（何もしません）\n", label.c_str());
            status_.lastPress = record;
            writeStatus();
            return;
        }
        std::fprintf(stderr, "[押し方] %s\n", label.c_str());
        if (action.type == ActionType::Screenshot) {
            // お知らせとミュート中の表示を写さない: 隠して、コンポジターが隠した画面を出すまで待ってから頼む
            overlay_.hideToast();
            if (badgeShown_) {
                overlay_.setBadge(nullptr);
                badgeShown_ = false;
                ::usleep(kBadgeHideWaitMs * 1000);
            }
            takingScreenshot_ = true;
        }
        PreparedAction prepared = prepareAction(action, config_, vr_);
        takingScreenshot_ = false;
        if (prepared.screenshot) {
            // 結果（音・お知らせ・状態ファイル）は、撮れたイベントが来てから出す（お知らせが写り込まないように）
            pendingShots_.push_back(
                {label, record, prepared.shot, nowMs() + static_cast<int64_t>(kScreenshotTimeoutSec * 1000)});
            return;
        }
        if (!prepared.done) {
            worker_.post(label, record, std::move(prepared.deferred));
            return;
        }
        finish(label, record, prepared.outcome);
        updateBadge();  // 頼めずに終わったスクリーンショットのあと、ミュート中なら出し直す
    }

    /** 作業用スレッドで終わったアクションの結果を受け取る。 */
    void collectFinished() {
        // スクリーンショット: 撮れた・失敗のイベントが来たもの、時間切れのもの
        const int64_t now = nowMs();
        bool shotFinished = false;
        for (auto it = pendingShots_.begin(); it != pendingShots_.end();) {
            bool ok = false;
            ActionOutcome outcome;
            if (vr_.screenshotResult(it->shot.handle, ok)) {
                outcome = finishScreenshot(vr_, it->shot, ok, config_.screenshotToSteam);
            } else if (now >= it->deadline) {
                outcome = screenshotTimedOut(it->shot);
            } else {
                ++it;
                continue;
            }
            const std::string label = it->label;
            const PressRecord record = it->record;
            it = pendingShots_.erase(it);
            shotFinished = true;
            finish(label, record, outcome);
        }
        // 撮れた・失敗・時間切れのどれでも、ミュート中ならすぐ出し直す
        if (shotFinished) updateBadge();
        for (const FinishedAction& done : worker_.takeFinished()) finish(done.label, done.record, done.outcome);
    }

    /**
     * ミュートの見張りの値を取り込み、読む間隔を決める。
     */
    void pollMic() {
        const unsigned generation = mic_.generation();
        if (generation != micGeneration_) {
            micGeneration_ = generation;
            const int muted = mic_.muted();
            if (muted != status_.micMuted) {
                if (muted >= 0 && status_.micMuted >= 0) {
                    std::fprintf(stderr, "[マイク] %s\n", muted ? "ミュートになりました" : "ミュートが解除されました");
                }
                status_.micMuted = muted;
                updateBadge();
                writeStatus();
            }
        }
        const bool needFast = (config_.muteBadge && status_.active) || panelRunning_;
        mic_.setIntervalMs(needFast ? kMicFastMs : kMicSlowMs);
    }

    /** 明るさ・パススルーを SteamVR から読む。 */
    void sampleVr() {
        int brightness = -1;
        int roomView = -1;
        if (vr_.connected()) {
            float gain = 0.0f;
            std::string error;
            if (vr_.getFloat("steamvr", "analogGain", gain, error)) {
                brightness = static_cast<int>(std::lround(brightnessGainToPercent(gain)));
            }
            bool known = false;
            if (vr_.sceneProcessId() != 0 && vr_.knownRoomView(known)) {
                roomView = known ? 1 : 0;
            } else if (vr_.getBool("camera", "enableConstructRoomView", known, error)) {
                roomView = known ? 1 : 0;
            }
        }
        if (brightness != status_.brightnessPercent || roomView != status_.roomView ||
            vr_.connected() != status_.vrConnected) {
            status_.brightnessPercent = brightness;
            status_.roomView = roomView;
            status_.vrConnected = vr_.connected();
            updateBadge();
            writeStatus();
        }
    }

    /** frame-perf-overlay の状態を読む。 */
    void sampleApps() {
        const pid_t pid = runningPid(kPerfOverlayName);
        const AppState state = pid > 0 ? AppState::Running
                                       : (isInstalled(kPerfOverlayName) ? AppState::Stopped : AppState::Missing);
        const int visible = state == AppState::Missing ? -1 : readPerfOverlayVisible();
        if (state != status_.perfOverlay || visible != status_.perfVisible) {
            status_.perfOverlay = state;
            status_.perfVisible = visible;
            writeStatus();
        }
    }

    /**
     * 視界の表示の時間を進める。
     * @param nowSec 今の時刻（秒）
     */
    void tickHud(double nowSec) { overlay_.tick(nowSec); }

    /** @return スクリーンショットの結果を待っているか（待っている間は SteamVR のイベントを細かく読む） */
    bool waitingForScreenshot() const { return !pendingShots_.empty(); }

    /** @return 次に視界の表示を進める時刻（ms、単調増加。予定がなければ -1） */
    int64_t nextHudTickMs() const {
        const double at = overlay_.nextTickAt();
        return std::isfinite(at) ? static_cast<int64_t>(at * 1000.0) : -1;
    }

    /**
     * 状態ファイルを書く（変わったときだけ）。
     * @param force 変わっていなくても書く
     */
    void writeStatus(bool force = false) {
        Status copy = status_;
        copy.updatedAt = 0;
        const std::string body = statusToJson(copy);
        if (!force && body == lastStatusBody_) return;
        lastStatusBody_ = body;
        copy.updatedAt = static_cast<long long>(std::time(nullptr));
        std::string error;
        if (!writeFileAtomically(statusFilePath(), statusToJson(copy), error) && error != lastStatusError_) {
            std::fprintf(stderr, "[状態] 書けません: %s\n", error.c_str());
            lastStatusError_ = error;
        }
    }

private:
    /**
     * アクションの結果を反映する（ログ・音・視界の表示・状態ファイル）。
     * @param label ログ用の名前
     * @param record 押し方
     * @param outcome 結果
     */
    void finish(const std::string& label, PressRecord record, const ActionOutcome& outcome) {
        std::fprintf(stderr, "[アクション] %s: %s %s\n", label.c_str(), outcome.ok ? "成功" : "失敗",
                     outcome.detail.c_str());
        playFeedbackSound(soundSettingsOf(config_), outcome.ok);
        record.ok = outcome.ok;
        record.value = outcome.value;
        record.reason = outcome.reason;
        record.detail = outcome.detail;
        status_.lastPress = record;
        if (outcome.ok) {
            // 結果で分かった状態はすぐ反映する（見張りの次の読み取りを待たない）
            if (record.action == ActionType::MicMuteToggle && !outcome.value.empty()) {
                status_.micMuted = outcome.value == "muted" ? 1 : 0;
                updateBadge();
            } else if (record.action == ActionType::BrightnessCycle) {
                status_.brightnessPercent = std::atoi(outcome.value.c_str());
            } else if (record.action == ActionType::RoomViewToggle) {
                status_.roomView = outcome.value == "on" ? 1 : 0;
                vr_.rememberRoomView(outcome.value == "on");
            } else if (record.action == ActionType::PerfOverlayToggle && !outcome.value.empty()) {
                status_.perfVisible = outcome.value == "shown" ? 1 : 0;
            }
        }
        writeStatus();
        HudToast toast;
        if (config_.hudEnabled && vr_.connected() && makeHudToast(record, config_.language, toast)) {
            overlay_.showToast(hud_.renderToast(toast), static_cast<double>(nowMs()) / 1000.0);
        }
    }

    /** ミュート中の表示を、今の状態に合わせて出す・消す。 */
    void updateBadge() {
        // スクリーンショットを撮っている間（頼む前から結果が来るまで）は出さない
        const bool want = config_.muteBadge && status_.active && status_.micMuted == 1 && vr_.connected() &&
                          pendingShots_.empty() && !takingScreenshot_;
        const int language = static_cast<int>(config_.language);
        if (want == badgeShown_ && (!want || language == badgeLanguage_)) return;
        if (want) {
            if (overlay_.setBadge(&hud_.renderBadge(config_.language))) {
                badgeShown_ = true;
                badgeLanguage_ = language;
            }
        } else {
            overlay_.setBadge(nullptr);
            badgeShown_ = false;
        }
    }

    Config config_;
    PressDetector detector_;
    FontSet fonts_;
    HudRenderer hud_;
    HudOverlay overlay_;
    VrClient vr_;
    ActionWorker worker_;
    /** 結果を待っているスクリーンショット。 */
    struct PendingShot {
        std::string label;
        PressRecord record;
        PendingScreenshot shot;
        int64_t deadline;  ///< これを過ぎたら失敗（ms、単調増加）
    };
    std::vector<PendingShot> pendingShots_;
    MicMonitor mic_;
    Status status_;
    unsigned micGeneration_ = 0;
    bool panelRunning_ = false;
    bool takingScreenshot_ = false;  ///< スクリーンショットを頼んでいる最中（ミュート中の表示を出さない）
    bool badgeShown_ = false;
    int badgeLanguage_ = -1;
    std::string lastStatusBody_;
    std::string lastStatusError_;
};

/**
 * evdev から読めるだけ読んで、aux（656）の押し・離しを判定に渡す。
 * @param fd gpio-keys
 * @param monotonicEvents イベントの時刻が CLOCK_MONOTONIC なら true（違えば読んだ時刻を使う）
 * @param daemon 常駐の中身
 * @return デバイスがまだ使えるなら true
 */
bool readKeyEvents(int fd, bool monotonicEvents, Daemon& daemon) {
    static bool dropping = false;  // SYN_DROPPED の後、次の SYN_REPORT までは捨てる
    input_event events[64];
    while (true) {
        const ssize_t n = ::read(fd, events, sizeof(events));
        if (n < 0) {
            if (errno == EINTR) continue;
            if (errno == EAGAIN || errno == EWOULDBLOCK) return true;
            std::fprintf(stderr, "[入力] 読めません: %s\n", std::strerror(errno));
            return false;
        }
        if (n == 0) return false;
        const size_t count = static_cast<size_t>(n) / sizeof(input_event);
        for (size_t i = 0; i < count; ++i) {
            const input_event& event = events[i];
            if (event.type == EV_SYN) {
                if (event.code == SYN_DROPPED) {
                    std::fprintf(stderr, "[入力] イベントを取りこぼしたので、途中の押し方を捨てます\n");
                    daemon.detector().reset();
                    dropping = true;
                } else if (event.code == SYN_REPORT) {
                    dropping = false;
                }
                continue;
            }
            if (dropping || event.type != EV_KEY || event.code != kAuxStandInCode) continue;
            if (event.value != 0 && event.value != 1) continue;  // 2 はオートリピート
            const int64_t at = monotonicEvents ? static_cast<int64_t>(event.input_event_sec) * 1000 +
                                                     static_cast<int64_t>(event.input_event_usec) / 1000
                                               : nowMs();
            daemon.handlePress(daemon.detector().onKey(event.value == 1, at));
        }
    }
}

/**
 * 常駐する。aux を置き換え、押し方を判定してアクションを動かし、終わるときに必ず元に戻す。
 * @param options コマンドライン
 * @return 終了コード
 */
int runDaemon(const Options& options) {
    int lockFd = -1;
    pid_t holderPid = 0;
    if (!acquireInstanceLock(lockFd, holderPid)) {
        std::fprintf(stderr, "[起動] すでに常駐しています（PID %d）。終わります\n", static_cast<int>(holderPid));
        return kExitCodeNoRestart;
    }

    ConfigWatcher watcher(options.configPath);
    const Config initialConfig = watcher.loadInitial();

    // 止める知らせは、待っている間（ppoll）にだけ受ける。作業用スレッドもこのマスクを引き継ぐので、
    // 知らせは必ずメインスレッドに届く
    sigset_t stopSignals;
    sigemptyset(&stopSignals);
    for (const int s : {SIGTERM, SIGINT, SIGHUP, SIGUSR2}) sigaddset(&stopSignals, s);
    sigset_t waitMask;
    sigprocmask(SIG_BLOCK, &stopSignals, &waitMask);
    for (const int s : {SIGTERM, SIGINT, SIGHUP, SIGUSR2}) sigdelset(&waitMask, s);

    const std::string device = findGpioKeysDevice();
    if (device.empty()) {
        std::fprintf(stderr, "[起動] gpio-keys が見つかりません\n");
        ::close(lockFd);
        return kExitCodeNoRestart;
    }
    std::string message;
    const int fd = keymap_guard::attach(device, message);
    if (fd < 0) {
        std::fprintf(stderr, "[起動] %s\n", message.c_str());
        ::close(lockFd);
        return kExitCodeNoRestart;
    }
    std::fprintf(stderr, "[対応表] %s\n", message.c_str());

    // 自分の読み取り口（このプロセスの fd）だけ、イベントの時刻を単調増加の時計にする（デバイスには影響しない）
    int clockId = CLOCK_MONOTONIC;
    const bool monotonicEvents = ::ioctl(fd, EVIOCSCLOCKID, &clockId) == 0;

    // 作業用スレッド・ミュートの見張りがメインスレッドを起こすためのパイプ
    int wakePipe[2] = {-1, -1};
    if (::pipe2(wakePipe, O_CLOEXEC | O_NONBLOCK) != 0) {
        std::fprintf(stderr, "[起動] pipe を作れません: %s\n", std::strerror(errno));
        ::close(lockFd);
        return 1;
    }

    Daemon daemon(initialConfig, wakePipe[1]);
    // 置き換えるのは、run_in_background か、設定パネルが動いているとき
    bool active = false;
    bool panelRunning = runningPid(kPanelName) > 0;
    daemon.setPanelRunning(panelRunning);
    /**
     * 置き換える・戻すを、設定とパネルの有無に合わせる。
     * @return 失敗したら false
     */
    const auto applyActive = [&]() {
        const bool want = daemon.config().runInBackground || panelRunning;
        if (want == active) return true;
        std::string result;
        const bool ok = want ? keymap_guard::activate(result) : keymap_guard::deactivate(result);
        std::fprintf(stderr, "[対応表] %s: %s\n",
                     want ? "aux を置き換えます" : "aux を元の動きに戻します（パネルを閉じたら戻す設定）", result.c_str());
        if (!ok) return false;
        active = want;
        daemon.setActive(active);
        return true;
    };
    if (!daemon.config().runInBackground && !panelRunning) {
        // 前回の置き換えが残っていれば戻してから待つ
        std::string result;
        keymap_guard::deactivate(result);
        std::fprintf(stderr, "[対応表] 設定パネルが動くまで aux は元の動きのままにします: %s\n", result.c_str());
        daemon.setActive(false);
    }
    int exitCode = 0;
    if (!applyActive()) {
        daemon.shutdown();
        keymap_guard::restore();
        ::close(lockFd);
        return kExitCodeNoRestart;
    }

    std::string lastVrMessage;
    int64_t nextVrTry = 0;
    int64_t nextVrPoll = 0;
    int64_t nextConfigCheck = nowMs() + kConfigCheckMs;
    int64_t nextPanelCheck = nowMs() + kPanelCheckMs;
    int64_t nextSample = 0;
    int64_t nextApps = 0;
    std::fprintf(stderr, "[起動] aux ボタンを待ちます（長押し %d ms・回数の区切り %d ms）\n", daemon.config().longPressMs,
                 daemon.config().multiPressGapMs);

    while (!gStopSignal) {
        int64_t now = nowMs();
        VrClient& vr = daemon.vr();
        // SteamVR: つながっていなければ数秒おきに試す（SteamVR を起動はしない）
        if (!options.noVr && !vr.connected() && now >= nextVrTry) {
            const VrClient::ConnectResult result = vr.connect(message);
            if (result == VrClient::ConnectResult::Ok) {
                std::fprintf(stderr, "[VR] SteamVR につながりました\n");
                lastVrMessage.clear();
                nextSample = now;
            } else if (message != lastVrMessage) {
                std::fprintf(stderr, "[VR] %s（3 秒おきに再試行。SteamVR を使うアクションは失敗の音になります）\n",
                             result == VrClient::ConnectResult::NotRunning ? "SteamVR が起動していないので待ちます"
                                                                           : ("接続に失敗: " + message).c_str());
                lastVrMessage = message;
            }
            nextVrTry = now + kVrRetryMs;
        }
        if (vr.connected() && now >= nextVrPoll) {
            vr.pollEvents();
            if (vr.quitRequested()) break;
            nextVrPoll = now + (daemon.waitingForScreenshot() ? kVrShotPollMs : kVrPollMs);
        }
        if (now >= nextConfigCheck) {
            if (watcher.reloadIfChanged(daemon.config())) {
                daemon.onConfigChanged();
                if (!applyActive()) {
                    exitCode = 1;
                    break;
                }
            }
            nextConfigCheck = now + kConfigCheckMs;
        }
        if (now >= nextPanelCheck) {
            panelRunning = runningPid(kPanelName) > 0;
            daemon.setPanelRunning(panelRunning);
            if (!applyActive()) {
                exitCode = 1;
                break;
            }
            nextPanelCheck = now + kPanelCheckMs;
        }
        if (now >= nextSample) {
            daemon.sampleVr();
            nextSample = now + kSampleMs;
        }
        if (now >= nextApps) {
            daemon.sampleApps();
            nextApps = now + kAppsCheckMs;
        }
        // 作業用スレッド・ミュートの見張りからの知らせ
        char drain[64];
        while (::read(wakePipe[0], drain, sizeof(drain)) > 0) {
        }
        daemon.collectFinished();
        daemon.pollMic();
        daemon.handlePress(daemon.detector().onTick(now));
        daemon.tickHud(static_cast<double>(now) / 1000.0);

        // 次に起きる時刻まで、入力・知らせ・止める知らせを待つ
        now = nowMs();
        int64_t wakeAt = std::min({nextConfigCheck, nextPanelCheck, nextSample, nextApps});
        if (!options.noVr) wakeAt = std::min(wakeAt, vr.connected() ? nextVrPoll : nextVrTry);
        const int64_t deadline = daemon.detector().nextDeadlineMs();
        if (deadline >= 0) wakeAt = std::min(wakeAt, deadline);
        const int64_t hudAt = daemon.nextHudTickMs();
        if (hudAt >= 0) wakeAt = std::min(wakeAt, hudAt);
        const int64_t waitMs = std::max<int64_t>(0, wakeAt - now);
        struct timespec timeout {};
        timeout.tv_sec = static_cast<time_t>(waitMs / 1000);
        timeout.tv_nsec = static_cast<long>((waitMs % 1000) * 1000000);
        pollfd pfds[2] = {{fd, POLLIN, 0}, {wakePipe[0], POLLIN, 0}};
        const int ready = ::ppoll(pfds, 2, &timeout, &waitMask);
        if (ready < 0) {
            if (errno == EINTR) continue;
            std::fprintf(stderr, "[入力] ppoll: %s\n", std::strerror(errno));
            exitCode = 1;
            break;
        }
        if (ready > 0 && pfds[0].revents != 0) {
            if ((pfds[0].revents & (POLLERR | POLLHUP | POLLNVAL)) != 0 || !readKeyEvents(fd, monotonicEvents, daemon)) {
                std::fprintf(stderr, "[入力] gpio-keys が使えなくなりました\n");
                exitCode = 1;
                break;
            }
        }
    }
    if (gStopSignal) {
        std::fprintf(stderr, "[終了] シグナル %d（%s）を受けました%s\n", static_cast<int>(gStopSignal),
                     strsignal(gStopSignal), gStopSignal == SIGUSR2 ? "（設定パネルの「終了」）" : "");
    }

    // 何より先に対応表を戻す（後の片付けで止まっても、aux が効かないままにならないように）
    const int restored = keymap_guard::restore();
    std::fprintf(stderr, "[対応表] %d 項目を戻しました: %s\n", restored, formatKeymap(readKeymap(fd)).c_str());
    daemon.shutdown();
    std::fprintf(stderr, "[終了] 終わりました\n");
    // fd は閉じない（atexit の戻しでも使う。プロセスの終わりで OS が閉じる）
    if (lockFd >= 0) ::close(lockFd);
    // 設定パネルの「終了」はユーザーが止めたので、systemd（Restart=always）に起動し直させない
    if (gStopSignal == SIGUSR2) return kExitCodeNoRestart;
    return exitCode;
}

}  // namespace

/**
 * エントリーポイント。
 * @param argc 引数の数
 * @param argv 引数
 * @return 終了コード
 */
int main(int argc, char** argv) {
    // journald でも行ごとにすぐ出るようにする
    std::setvbuf(stderr, nullptr, _IOLBF, 0);
    installSignalHandlers();
    keymap_guard::installCrashHandlers();

    Options options;
    if (!parseOptions(argc, argv, options)) {
        printUsage();
        return kExitCodeUsage;
    }
    switch (options.mode) {
        case Options::Mode::Help: printUsage(); return 0;
        case Options::Mode::Version: std::printf("frame-aux-shortcuts %s\n", FRAME_AUX_SHORTCUTS_VERSION); return 0;
        case Options::Mode::Restore: return runRestore();
        case Options::Mode::ShowKeymap: return runShowKeymap();
        case Options::Mode::TestAction: return runTestAction(options);
        case Options::Mode::SendOsc: return runSendOsc(options);
        case Options::Mode::DumpHud: return runDumpHud(options);
        case Options::Mode::Run: break;
    }
    return runDaemon(options);
}
