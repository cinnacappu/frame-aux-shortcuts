// frame-aux-shortcuts-panel: aux ショートカットの設定パネル（SteamVR のダッシュボードのタブ）。
// 設定ファイル（~/.config/frame-aux-shortcuts/config.json）を書き換え、本体の状態ファイルを読んで表示する。
// 本体（frame-aux-shortcuts）とのやりとりはファイルだけ（「終了」のときだけ、本体に SIGUSR2 を送る）。
// 描き方・ダッシュボードへの出し方・自動起動・更新の帯は frame-perf-overlay と同じ作り。
#include "autostart.h"
#include "command.h"
#include "commands.h"
#include "config.h"
#include "dashboard_overlay.h"
#include "draw.h"
#include "frame_apps.h"  // vendor/frame-apps/cpp
#include "i18n.h"
#include "osc.h"
#include "panel_state.h"
#include "runtime.h"
#include "settings_panel.h"
#include "status.h"
#include "theme.h"
#include "update_check.h"  // vendor/frame-updater/cpp

#include <cairo.h>
#include <fcntl.h>
#include <signal.h>
#include <sys/file.h>
#include <unistd.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <string>
#include <thread>
#include <vector>

// バージョンは CMakeLists.txt の project(VERSION) から入る
#ifndef FRAME_AUX_SHORTCUTS_VERSION
#define FRAME_AUX_SHORTCUTS_VERSION "unknown"
#endif

namespace {

volatile std::sig_atomic_t gStopRequested = 0;
volatile std::sig_atomic_t gShowRequested = 0;  ///< SIGUSR1（2 つ目の起動から）でダッシュボードのこのタブを開く

/** 「終了」・ダッシュボードの「閉じる」で終わったときの終了コード（systemd の RestartPreventExitStatus= に書く）。 */
constexpr int kExitCodeUserQuit = 3;
constexpr const char* kPanelUnit = "frame-aux-shortcuts-panel.service";

constexpr int kThumbnailSize = 256;             ///< ダッシュボードのサムネイルの一辺（px）
constexpr double kPointerPollSec = 0.05;        ///< 操作している間、マウスのイベントを確かめる間隔
constexpr double kVisiblePollSec = 0.1;         ///< 見えているだけのときの間隔
constexpr double kPointerHoldSec = 3.0;         ///< 最後のマウスのイベントから、この秒数は操作中とみなす
constexpr double kSlowCheckSec = 0.5;           ///< 設定ファイル・状態ファイル・見えているかなどを確かめる間隔
constexpr double kAppsRefreshSec = 5.0;         ///< アプリタブを見ている間、ほかのアプリの状態を見直す間隔
constexpr double kAutostartRefreshSec = 3.0;    ///< 見えている間に自動起動の状態を読み直す間隔
constexpr double kFieldCloseGraceSec = 0.3;     ///< キーボードが閉じてから入力中の目印を消すまで（すぐ開き直すときのちらつき止め）

/** コマンドラインの内容。 */
struct Options {
    enum class Mode { Panel, DumpPng, Help, Version };
    Mode mode = Mode::Panel;
    std::string configPath;
    std::string pngPath;
    std::string thumbnailPngPath;
    int thumbnailSize = 256;
    std::string language;        ///< PNG の書き出しで使う言語（ja / en）
    std::string tab = "assign";  ///< PNG の書き出しで見せるタブ（パネルとして動くときは、最初に見せるタブ）
    bool tabGiven = false;       ///< --tab を指定した（パネルとして動くとき、覚えておいたタブより優先する）
    int slot = 0;                ///< PNG の書き出しで選んでおくカード
    std::string previewUpdate = "uptodate";  ///< 更新の帯の状態
    std::string previewPerf = "running";     ///< frame-perf-overlay の状態（running / stopped / missing）
    std::string previewDaemon = "running";   ///< 本体の状態（running / standby / stopped）
    std::string previewAutostart = "enabled";  ///< 自動起動の状態（enabled / disabled / notinstalled / busy / failed）
    bool previewMuted = true;    ///< マイクがミュート中として描く
    bool previewQuit = false;    ///< 「もう一度押すと終了」の状態で描く
    bool previewReset = false;   ///< 「もう一度押すと既定に戻す」の状態で描く
    bool previewBgOff = false;   ///< 「パネルを閉じても aux を使う」をオフとして描く
    int previewCommands = -1;    ///< 見本のコマンドをこの数だけ入れる（-1 なら設定ファイルのまま）
    bool previewFolder = false;  ///< 見本のフォルダのコマンドを 2 つ入れる
    int previewAssignCommand = 0;  ///< 5 回押しにこの番号の見本のコマンドを割り当てる（0 なら割り当てない）
    std::string previewPicker;   ///< 割り当ての列の側（actions / commands:<ページ>）。空なら選んだカードに合わせる
    std::string previewSheet;    ///< シート（add / edit / invalid / quote / tested / failed）
    std::string previewDelete;   ///< 「消す」の 2 回目を待つコマンドの id
    std::string previewTested;   ///< 一覧で［試す］を押したコマンドの id（:fail で失敗）
    std::string previewScroll;   ///< 一覧のスクロール（px か end）
    std::string previewDrag;     ///< 並び替えの途中（<id>:<行の数>）
    std::string previewTyping;   ///< 入力中の欄（<欄 0 / 1>:<文字>。文字は空でもよい）
    std::string previewApps;     ///< アプリの一覧・確認（list / confirm:<呼び名> / confirm:menu / list-confirm:<呼び名>）
    std::string previewAppsScroll;  ///< アプリの一覧のスクロール（px か end）
    int previewAppsExtra = 0;    ///< 一覧に見本のアプリをこの数だけ足す（アプリが増えたときのスクロールの確認）
    std::string previewKeyboard = "installed";  ///< frame-jp-keyboard の状態（running / installed / missing）
    std::string previewBusy;     ///< Konsole で実行中にするアプリの呼び名（, 区切り。menu でメニュー）
    std::string previewLaunch;   ///< 確認に出す開けなかった理由（busy / nodisplay / nokonsole / failed）
    bool keyboardBuffered = false;  ///< SteamVR のキーボードを今までの方式（Minimal でない）で開く
};

/**
 * SIGTERM / SIGINT を受けたら止める印をつける。
 * @param signal 受けたシグナル（使わない）
 */
void onStopSignal(int /*signal*/) {
    gStopRequested = 1;
}

/**
 * SIGUSR1 を受けたら、ダッシュボードのこのタブを開く印をつける。
 * @param signal 受けたシグナル（使わない）
 */
void onShowSignal(int /*signal*/) {
    gShowRequested = 1;
}

/** シグナルのハンドラーを入れる。 */
void installSignalHandlers() {
    struct sigaction stop {};
    stop.sa_handler = onStopSignal;
    sigemptyset(&stop.sa_mask);
    sigaction(SIGTERM, &stop, nullptr);
    sigaction(SIGINT, &stop, nullptr);
    struct sigaction show {};
    show.sa_handler = onShowSignal;
    sigemptyset(&show.sa_mask);
    sigaction(SIGUSR1, &show, nullptr);
    signal(SIGPIPE, SIG_IGN);
}

/**
 * 単調増加の時計で今の時刻を秒で返す。
 * @return 秒
 */
double nowSeconds() {
    using namespace std::chrono;
    return duration<double>(steady_clock::now().time_since_epoch()).count();
}

/**
 * 止める印がつくまで、または指定時間が経つまで待つ。
 * @param seconds 待つ秒数
 */
void sleepInterruptible(double seconds) {
    const double end = nowSeconds() + seconds;
    while (!gStopRequested && !gShowRequested) {
        const double left = end - nowSeconds();
        if (left <= 0) break;
        std::this_thread::sleep_for(std::chrono::duration<double>(std::fmin(left, 0.5)));
    }
}

/** 使い方を表示する。 */
void printUsage() {
    std::printf(
        "使い方: frame-aux-shortcuts-panel [オプション]\n"
        "  （なし）             SteamVR のダッシュボードに設定パネルを出す（SteamVR が無ければ待つ）。\n"
        "                       すでに動いていれば、そちらのタブを開いて終わる\n"
        "  --dump-png PATH      OpenVR なしで設定パネルの画像を PNG に書き出して終わる（見た目の確認用）\n"
        "      --tab T          タブ（assign / brightness / press / commands / display / app。既定 assign）\n"
        "      --slot N         割り当てタブで選んでおくカード（0〜4 が 1〜5 回押し、5 が長押し）\n"
        "      --language L     言語（ja / en。既定は設定ファイルの言語）\n"
        "      --preview-update S     更新の帯（uptodate / checking / available / manual / confirm / installing /\n"
        "                             installed / checkfailed / installfailed / unknown）\n"
        "      --preview-perf S       frame-perf-overlay（running / stopped / missing）\n"
        "      --preview-daemon S     本体（running / standby / handedback / stopped）\n"
        "      --preview-autostart S  自動起動（enabled / disabled / notinstalled / busy / failed）\n"
        "      --preview-unmuted      マイクがミュートでないとして描く（既定はミュート中）\n"
        "      --preview-bg-off       「パネルを閉じても aux を使う」をオフとして描く\n"
        "      --preview-quit / --preview-reset  確認の 2 回目を待っている状態で描く\n"
        "      --preview-commands N   見本のコマンドを N 個にする（--preview-folder でフォルダの分を 2 つ足す）\n"
        "      --preview-assign-command K  5 回押しに見本の K 番目のコマンドを割り当てる\n"
        "      --preview-picker S     割り当ての列（actions / commands:<ページ（0 から）>）\n"
        "      --preview-sheet S      コマンドのシート（add / edit / invalid / quote / tested / failed /\n"
        "                             presets / presets-nopc / presets-manual / presets-badip / presets-sent /\n"
        "                             presets-speaker）\n"
        "      --preview-delete ID / --preview-tested ID[:fail]  一覧の「消す」の確認・［試す］の結果\n"
        "      --preview-scroll PX|end  コマンドの一覧のスクロール\n"
        "      --preview-drag ID:ROWS   並び替えの途中（ROWS 行ぶん動かした見た目）\n"
        "      --preview-typing F:TEXT  シートの欄 F（0 = 名前、1 = コマンド）にキーボードで入力している途中の見た目\n"
        "                       （TEXT の中の | が入力位置。無ければ末尾）\n"
        "      --preview-apps S       アプリの一覧・確認（list / confirm:<呼び名> / confirm:menu / list-confirm:<呼び名>）\n"
        "      --preview-apps-scroll PX|end  アプリの一覧のスクロール\n"
        "      --preview-apps-extra N  一覧に見本のアプリを N 個足す（増えたときのスクロールの確認）\n"
        "      --preview-keyboard S   frame-jp-keyboard（running / installed / missing。perf は --preview-perf）\n"
        "      --preview-busy KEYS    Konsole で実行中のアプリ（呼び名を , 区切り。menu でメニュー）\n"
        "      --preview-launch S     確認に出す開けなかった理由（busy / nodisplay / nokonsole / failed）\n"
        "      --thumbnail-png PATH   あわせてダッシュボードのサムネイル（アイコンと同じ絵）も書き出す\n"
        "      --thumbnail-size N     その一辺（既定 256）\n"
        "  --keyboard-buffered  SteamVR のキーボードを今までの方式で開く（既定は Minimal で 1 文字ずつ組み立てる。\n"
        "                       環境変数 FRAME_AUX_KEYBOARD=buffered でも同じ）\n"
        "  --tab T              パネルとして動くとき、最初に見せるタブ（無ければ最後に選んだタブ）\n"
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
        if (arg == "--dump-png" && hasNext) {
            options.mode = Options::Mode::DumpPng;
            options.pngPath = argv[++i];
        } else if (arg == "--tab" && hasNext) {
            options.tab = argv[++i];
            options.tabGiven = true;
        } else if (arg == "--slot" && hasNext) {
            options.slot = std::atoi(argv[++i]);
        } else if (arg == "--language" && hasNext) {
            options.language = argv[++i];
        } else if (arg == "--preview-update" && hasNext) {
            options.previewUpdate = argv[++i];
        } else if (arg == "--preview-perf" && hasNext) {
            options.previewPerf = argv[++i];
        } else if (arg == "--preview-daemon" && hasNext) {
            options.previewDaemon = argv[++i];
        } else if (arg == "--preview-autostart" && hasNext) {
            options.previewAutostart = argv[++i];
        } else if (arg == "--preview-unmuted") {
            options.previewMuted = false;
        } else if (arg == "--preview-bg-off") {
            options.previewBgOff = true;
        } else if (arg == "--preview-commands" && hasNext) {
            options.previewCommands = std::atoi(argv[++i]);
        } else if (arg == "--preview-folder") {
            options.previewFolder = true;
        } else if (arg == "--preview-assign-command" && hasNext) {
            options.previewAssignCommand = std::atoi(argv[++i]);
        } else if (arg == "--preview-picker" && hasNext) {
            options.previewPicker = argv[++i];
        } else if (arg == "--preview-sheet" && hasNext) {
            options.previewSheet = argv[++i];
        } else if (arg == "--preview-delete" && hasNext) {
            options.previewDelete = argv[++i];
        } else if (arg == "--preview-tested" && hasNext) {
            options.previewTested = argv[++i];
        } else if (arg == "--preview-scroll" && hasNext) {
            options.previewScroll = argv[++i];
        } else if (arg == "--preview-typing" && hasNext) {
            options.previewTyping = argv[++i];
        } else if (arg == "--preview-apps" && hasNext) {
            options.previewApps = argv[++i];
        } else if (arg == "--preview-apps-scroll" && hasNext) {
            options.previewAppsScroll = argv[++i];
        } else if (arg == "--preview-apps-extra" && hasNext) {
            options.previewAppsExtra = std::atoi(argv[++i]);
        } else if (arg == "--preview-keyboard" && hasNext) {
            options.previewKeyboard = argv[++i];
        } else if (arg == "--preview-busy" && hasNext) {
            options.previewBusy = argv[++i];
        } else if (arg == "--preview-launch" && hasNext) {
            options.previewLaunch = argv[++i];
        } else if (arg == "--keyboard-buffered") {
            options.keyboardBuffered = true;
        } else if (arg == "--preview-drag" && hasNext) {
            options.previewDrag = argv[++i];
        } else if (arg == "--preview-quit") {
            options.previewQuit = true;
        } else if (arg == "--preview-reset") {
            options.previewReset = true;
        } else if (arg == "--thumbnail-png" && hasNext) {
            options.thumbnailPngPath = argv[++i];
        } else if (arg == "--thumbnail-size" && hasNext) {
            options.thumbnailSize = std::atoi(argv[++i]);
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
 * タブの名前を読む。
 * @param name assign / brightness / press / commands / display / app
 * @return タブ（知らなければ割り当て）
 */
PanelTab parseTab(const std::string& name) {
    PanelTab tab = PanelTab::Assign;
    return parsePanelTab(name, tab) ? tab : PanelTab::Assign;
}

/**
 * 更新チェッカーの設定を作る（スクリプトの場所は install.sh が置いた ~/.local/share/frame-aux-shortcuts/）。
 * @return 設定
 */
frame_updater::UpdaterConfig makeUpdaterConfig() {
    const char* xdgData = std::getenv("XDG_DATA_HOME");
    std::string dataHome;
    if (xdgData != nullptr && xdgData[0] != '\0') {
        dataHome = xdgData;
    } else {
        const char* home = std::getenv("HOME");
        dataHome = std::string(home != nullptr ? home : ".") + "/.local/share";
    }
    frame_updater::UpdaterConfig cfg;
    cfg.script = dataHome + "/frame-aux-shortcuts/frame-update.sh";
    cfg.app = "frame-aux-shortcuts";
    cfg.repo = "sasaken1102r/frame-aux-shortcuts";
    cfg.currentVersion = FRAME_AUX_SHORTCUTS_VERSION;
    cfg.assetPattern = "frame-aux-shortcuts-{version}.tar.gz";
    return cfg;
}

/**
 * ほかのアプリの一覧の設定（このアプリで使うものと、その理由）。
 * @return 設定
 */
frame_apps::ManagerConfig makeAppsConfig() {
    const UiText& ja = uiText(Language::Ja);
    const UiText& en = uiText(Language::En);
    frame_apps::ManagerConfig cfg;
    cfg.self = "frame-aux-shortcuts";
    cfg.related = {
        {"frame-perf-overlay", ja.appsReasonPerf, en.appsReasonPerf},
        {"frame-jp-keyboard", ja.appsReasonKeyboard, en.appsReasonKeyboard},
    };
    return cfg;
}

/**
 * 見た目の確認用に、新しい版の確認の状態をダミーで作る。
 * @param name 状態の名前
 * @return ダミーの状態（知らない名前なら uptodate）
 */
frame_updater::UpdateStatus fakeUpdateStatus(const std::string& name) {
    using frame_updater::UpdateState;
    frame_updater::UpdateStatus s;
    s.current = FRAME_AUX_SHORTCUTS_VERSION;
    s.latest = FRAME_AUX_SHORTCUTS_VERSION;
    s.checkedAt = std::time(nullptr);
    const std::string next = "0.3.0";
    if (name == "available" || name == "confirm") {
        s.state = UpdateState::Available;
        s.latest = next;
        s.installable = true;
        s.notes = "Adds the screenshot shortcut and fixes the mute indicator after SteamVR restarts.";
        s.notesJa = "スクリーンショットのショートカットを足し、SteamVR を再起動したあとのミュート中の表示を直しました。";
    } else if (name == "manual") {
        s.state = UpdateState::Available;
        s.latest = next;
        s.installable = false;
        s.reason = "no-checksums";
    } else if (name == "checking") {
        s.state = UpdateState::Unknown;
        s.checking = true;
    } else if (name == "unknown") {
        s.state = UpdateState::Unknown;
    } else if (name == "installing") {
        s.state = UpdateState::Installing;
        s.version = next;
        s.step = "download";
    } else if (name == "installed") {
        s.state = UpdateState::Installed;
        s.version = next;
    } else if (name == "checkfailed") {
        s.state = UpdateState::CheckFailed;
        s.error = "network";
    } else if (name == "installfailed") {
        s.state = UpdateState::InstallFailed;
        s.version = next;
        s.error = "checksum-mismatch";
    } else {
        s.state = UpdateState::UpToDate;
    }
    return s;
}

/**
 * --dump-png: OpenVR なしで設定パネル（とサムネイル）を描いて PNG に書き出す。
 * @param options コマンドライン
 * @return 終了コード
 */
int runDumpPng(const Options& options) {
    ConfigWatcher watcher(options.configPath);
    Config config = watcher.loadInitial();
    if (!options.language.empty()) parseLanguage(options.language, config.language);
    if (options.previewBgOff) config.runInBackground = false;
    FontSet fonts;
    fonts.load(kFontRegular, kFontBold);
    SettingsPanel panel(fonts);
    panel.setTab(parseTab(options.tab));
    panel.setSelectedSlot(std::max(0, std::min(kLongPressSlot, options.slot)));
    if (options.previewQuit) panel.armQuitForPreview();
    if (options.previewReset) panel.armResetForPreview();
    if (options.previewUpdate == "confirm") panel.armUpdateConfirmForPreview();

    PanelView view;
    view.now = std::time(nullptr);
    view.daemon = options.previewDaemon == "stopped"   ? PanelView::Daemon::Stopped
                  : options.previewDaemon == "standby" ? PanelView::Daemon::Standby
                  : options.previewDaemon == "handedback" ? PanelView::Daemon::HandedBack
                                                       : PanelView::Daemon::Running;
    view.haveStatus = view.daemon != PanelView::Daemon::Stopped;
    Status& s = view.status;
    s.active = view.daemon == PanelView::Daemon::Running;
    s.handedBack = view.daemon == PanelView::Daemon::HandedBack;
    s.micMuted = options.previewMuted ? 1 : 0;
    s.brightnessPercent = 60;
    s.roomView = 0;
    s.perfOverlay = options.previewPerf == "missing"   ? AppState::Missing
                    : options.previewPerf == "stopped" ? AppState::Stopped
                                                       : AppState::Running;
    s.perfVisible = s.perfOverlay == AppState::Running ? 0 : -1;
    s.lastPress.kind = PressRecord::Kind::Count;
    s.lastPress.count = 2;
    s.lastPress.action = config.press[1].type;
    s.lastPress.value = "60";
    s.lastPress.at = view.now - 60;
    const std::string& a = options.previewAutostart;
    view.autostart.state = a == "disabled"       ? AutostartStatus::State::Disabled
                           : a == "notinstalled" ? AutostartStatus::State::NotInstalled
                                                 : AutostartStatus::State::Enabled;
    view.autostart.busy = a == "busy";
    view.autostart.failed = a == "failed";
    view.update = fakeUpdateStatus(options.previewUpdate);

    // コマンドの見本
    if (options.previewCommands >= 0) {
        const bool ja = config.language == Language::Ja;
        const char* names[][2] = {
            {"スピーカーのミュート", "Mute speakers"},
            {"PC の VRChat をミュート", "Mute VRChat on the PC"},
            {"録画の開始・停止", "Start / stop recording"},
            {"mic-tuner のパネルを開く", "Open the mic-tuner panel"},
            {"音量を 50% に", "Volume to 50%"},
            {"リフレッシュレート 90 / 120", "Refresh rate 90 / 120"},
            {"Discord に「離席」", "Set Discord to away"},
            {"正面のリセット", "Reset the forward view"},
            {"Wi-Fi をつなぎ直す", "Reconnect Wi-Fi"},
            {"スクショを PC に送る", "Send screenshots to the PC"},
            {"とても長い名前のコマンドが入りきらないときの見え方を確かめるための名前", "A very long command name to check how it looks when it doesn't fit"},
        };
        const char* lines[] = {
            "wpctl set-mute @DEFAULT_AUDIO_SINK@ toggle", "/home/steamos/bin/vrchat-voice.sh",
            "/home/steamos/bin/rec-toggle.sh", "/home/steamos/bin/open-mic-tuner.sh",
            "wpctl set-volume @DEFAULT_AUDIO_SINK@ 0.5", "/home/steamos/bin/refresh-toggle.sh",
            "/home/steamos/bin/discord-away.sh \"離席中です\"", "/home/steamos/bin/recenter.sh",
            "/home/steamos/bin/wifi-reconnect.sh --interface wlan0 --retry 3 --timeout 10", "/home/steamos/bin/send-shots.sh",
            "/home/steamos/bin/long.sh --a-very-long-option-name value --another-very-long-option-name value",
        };
        config.commands.clear();
        config.commandOrder.clear();
        for (int i = 0; i < options.previewCommands; ++i) {
            CommandDef command;
            command.id = "c" + std::to_string(i + 1);
            command.name = names[i % 11][ja ? 0 : 1];
            std::string error;
            splitCommandLine(lines[i % 11], command.argv, error);
            config.commands.push_back(command);
        }
    }
    if (options.previewFolder) {
        for (const char* file : {"backup-config.sh", "toggle-hotspot"}) {
            CommandDef command;
            command.id = std::string(kFolderCommandPrefix) + file;
            const std::string name = file;
            command.name = name.substr(0, name.find('.'));
            command.argv = {"/home/steamos/.config/frame-aux-shortcuts/commands/" + name};
            command.fromFolder = true;
            view.folderCommands.push_back(command);
        }
    }
    if (options.previewAssignCommand > 0) {
        config.press[4].type = ActionType::Command;
        config.press[4].commandId = "c" + std::to_string(options.previewAssignCommand);
        config.press[0].type = ActionType::Command;
        config.press[0].commandId = "c1";
    }
    if (options.previewPicker == "actions") panel.setPickerForPreview(false, 0);
    if (options.previewPicker.rfind("commands", 0) == 0) {
        const size_t colon = options.previewPicker.find(':');
        panel.setPickerForPreview(true, colon == std::string::npos ? 0 : std::atoi(options.previewPicker.c_str() + colon + 1));
    }
    panel.render(config, view);  // 一覧（並び）を覚えさせる
    if (!options.previewSheet.empty()) {
        const std::string& sheet = options.previewSheet;
        const CommandDef* first = config.commands.empty() ? nullptr : &config.commands[0];
        const bool presets = sheet.rfind("presets", 0) == 0;
        panel.openSheet(sheet == "add" || sheet == "invalid" || presets ? nullptr : first);
        if (sheet == "add" || sheet == "invalid") panel.setPresetForPreview(false, CommandPreset::VrchatMute, true);
        if (presets) {
            const bool manual = sheet == "presets-manual" || sheet == "presets-badip";
            panel.setPresetForPreview(true, sheet == "presets-speaker" ? CommandPreset::SpeakerMute : CommandPreset::VrchatMute,
                                      !manual);
            panel.setDetectedPeer(sheet != "presets-nopc", "192.168.0.12");
            if (sheet == "presets-manual") panel.setSheetText(SettingsPanel::kFieldIp, "192.168.0.12");
            if (sheet == "presets-badip") panel.setSheetText(SettingsPanel::kFieldIp, "192.168.0.");
            if (sheet == "presets-sent") panel.setPresetTestResult(SettingsPanel::PresetTestResult::Sent);
            if (sheet == "presets-nopc") panel.setPresetTestResult(SettingsPanel::PresetTestResult::NoPc);
        }
        if (sheet == "invalid") panel.setSheetText(1, "wpctl set-mute @DEFAULT_AUDIO_SOURCE@ toggle");
        if (sheet == "quote") panel.setSheetText(1, "notify-send \"aux");
        if (sheet == "tested") panel.setSheetTestResult(true);
        if (sheet == "failed") panel.setSheetTestResult(false);
    }
    if (!options.previewTyping.empty()) {
        const size_t colon = options.previewTyping.find(':');
        const int field = std::atoi(options.previewTyping.c_str());
        // 文字の中の | が入力位置（無ければ末尾）
        std::string text = colon == std::string::npos ? std::string() : options.previewTyping.substr(colon + 1);
        const size_t bar = text.find('|');
        if (bar != std::string::npos) text.erase(bar, 1);
        panel.setSheetText(field, text);
        panel.setActiveField(field);
        panel.setSheetText(field, text, bar);
    }
    if (!options.previewDelete.empty()) panel.armDeleteForPreview(options.previewDelete);
    if (!options.previewTested.empty()) {
        const size_t colon = options.previewTested.find(':');
        panel.setRowTestResult(options.previewTested.substr(0, colon), colon == std::string::npos);
    }
    if (!options.previewScroll.empty()) {
        panel.setListScrollForPreview(options.previewScroll == "end" ? 1e9 : std::atof(options.previewScroll.c_str()));
    }
    if (!options.previewDrag.empty()) {
        const size_t colon = options.previewDrag.find(':');
        panel.dragForPreview(options.previewDrag.substr(0, colon),
                             colon == std::string::npos ? 1 : std::atoi(options.previewDrag.c_str() + colon + 1));
    }
    {
        // ほかのアプリ: 見本の状態（eye は動作中、mic は入っている、perf と keyboard は指定のとおり）
        using frame_apps::AppState;
        frame_apps::ManagerConfig previewConfig = makeAppsConfig();
        previewConfig.fetch.cacheDir.clear();  // 見本は同梱の一覧で（取ってきた一覧は使わない）
        frame_apps::AppsManager apps(previewConfig);
        const auto stateOf = [](const std::string& name) {
            return name == "missing" ? AppState::Missing : name == "running" ? AppState::Running : AppState::Installed;
        };
        std::vector<std::string> busy;
        std::string rest = options.previewBusy;
        while (!rest.empty()) {
            const size_t comma = rest.find(',');
            const std::string key = rest.substr(0, comma);
            busy.push_back(key == "menu" ? std::string() : key);
            rest = comma == std::string::npos ? std::string() : rest.substr(comma + 1);
        }
        apps.setForPreview({{"frameeyeosc", AppState::Running},
                            {"frame-mic-tuner", AppState::Installed},
                            {"frame-perf-overlay", stateOf(options.previewPerf == "stopped" ? "installed" : options.previewPerf)},
                            {"frame-jp-keyboard", stateOf(options.previewKeyboard)}},
                           busy);
        view.apps = apps.entries();
        view.menuBusy = apps.busy("");
        for (int i = 0; i < options.previewAppsExtra; ++i) {
            frame_apps::Entry extra;
            extra.app = {"frame-example-" + std::to_string(i + 1), "eye", "Ex", "[今後のアプリの説明がここに入ります]",
                         "[A future app's description]", "frame-example.service", ".local/bin/frame-example",
                         "frame-example-128.png", ""};
            extra.state = AppState::Missing;
            view.apps.push_back(extra);
        }
        const std::string& a = options.previewApps;
        if (!a.empty()) {
            const size_t colon = a.find(':');
            const std::string kind = a.substr(0, colon);
            const std::string key = colon == std::string::npos ? std::string() : a.substr(colon + 1);
            const double scroll = options.previewAppsScroll == "end" ? 1e9 : std::atof(options.previewAppsScroll.c_str());
            panel.openAppsForPreview(kind == "list" || kind == "list-confirm", kind == "list" ? std::string() : key, scroll);
            const std::string& l = options.previewLaunch;
            if (!l.empty()) {
                using frame_apps::LaunchResult;
                panel.setAppsLaunchResult(l == "busy"        ? LaunchResult::Busy
                                          : l == "nodisplay" ? LaunchResult::NoDisplay
                                          : l == "nokonsole" ? LaunchResult::NoKonsole
                                                             : LaunchResult::Failed);
            }
        }
    }
    panel.render(config, view);
    if (!panel.writePng(options.pngPath)) {
        std::fprintf(stderr, "PNG を書き出せませんでした: %s\n", options.pngPath.c_str());
        return 1;
    }
    std::printf("PNG を書き出しました: %s（%dx%d）\n", options.pngPath.c_str(), SettingsPanel::kWidth,
                SettingsPanel::kHeight);
    if (!options.thumbnailPngPath.empty()) {
        std::vector<uint8_t> rgba;
        renderThumbnail(fonts, options.thumbnailSize, rgba, options.thumbnailPngPath);
        std::printf("サムネイルを書き出しました: %s（%dx%d）\n", options.thumbnailPngPath.c_str(), options.thumbnailSize,
                    options.thumbnailSize);
    }
    return 0;
}

/**
 * ダッシュボードのサムネイルを作る。アプリのアイコン（install.sh が入れた
 * ~/.local/share/icons/hicolor/256x256/apps/frame-aux-shortcuts.png、無ければ実行ファイルの近くの
 * contrib/icons/frame-aux-shortcuts-256.png）を読んで縮める。どれも無ければ cairo で描いた代わりの絵。
 * @param fonts 代わりの絵に使うフォント
 * @param size 一辺の px
 * @param rgba 非乗算済み RGBA の書き込み先
 */
void loadThumbnail(const FontSet& fonts, int size, std::vector<uint8_t>& rgba) {
    std::vector<std::string> candidates;
    const char* xdgData = std::getenv("XDG_DATA_HOME");
    const char* home = std::getenv("HOME");
    const std::string dataHome = xdgData != nullptr && xdgData[0] != '\0' ? std::string(xdgData)
                                                                         : std::string(home != nullptr ? home : ".") + "/.local/share";
    candidates.push_back(dataHome + "/icons/hicolor/256x256/apps/frame-aux-shortcuts.png");
    char exe[4096] = {};
    const ssize_t n = ::readlink("/proc/self/exe", exe, sizeof(exe) - 1);
    if (n > 0) {
        std::string dir(exe, static_cast<size_t>(n));
        dir = dir.substr(0, dir.find_last_of('/'));
        candidates.push_back(dir + "/contrib/icons/frame-aux-shortcuts-256.png");     // リリースの tar.gz
        candidates.push_back(dir + "/../contrib/icons/frame-aux-shortcuts-256.png");  // ビルドしたフォルダ（build/）
    }
    for (const std::string& path : candidates) {
        if (::access(path.c_str(), R_OK) != 0) continue;
        cairo_surface_t* icon = cairo_image_surface_create_from_png(path.c_str());
        if (cairo_surface_status(icon) != CAIRO_STATUS_SUCCESS || cairo_image_surface_get_width(icon) <= 0) {
            cairo_surface_destroy(icon);
            continue;
        }
        cairo_surface_t* surface = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, size, size);
        cairo_t* cr = cairo_create(surface);
        const double scale = static_cast<double>(size) / cairo_image_surface_get_width(icon);
        cairo_scale(cr, scale, static_cast<double>(size) / cairo_image_surface_get_height(icon));
        cairo_set_source_surface(cr, icon, 0, 0);
        cairo_pattern_set_filter(cairo_get_source(cr), CAIRO_FILTER_BEST);
        cairo_paint(cr);
        cairo_destroy(cr);
        surfaceToRgba(surface, rgba);
        cairo_surface_destroy(surface);
        cairo_surface_destroy(icon);
        std::fprintf(stderr, "[VR] サムネイルに %s を使います\n", path.c_str());
        return;
    }
    std::fprintf(stderr, "[VR] アイコンの PNG が無いので、代わりの絵をサムネイルにします\n");
    renderThumbnail(fonts, size, rgba);
}

/**
 * 本体の状態を読む。
 * @param view 書き込み先（daemon・haveStatus・status・now）
 * @param lastText 前に読んだ状態ファイルの中身（変わったかどうかを見るため。書き換える）
 * @return 前と変わったら true
 */
bool readDaemonState(PanelView& view, std::string& lastText) {
    view.now = std::time(nullptr);
    const pid_t pid = runningPid(kDaemonName);
    std::string text;
    Status status;
    std::string error;
    const bool read = pid > 0 && readTextFile(statusFilePath(), text) && parseStatusText(text, status, error) &&
                      status.pid == static_cast<int>(pid);
    const PanelView::Daemon daemon = pid <= 0                      ? PanelView::Daemon::Stopped
                                     : read && status.active       ? PanelView::Daemon::Running
                                     : read && status.handedBack   ? PanelView::Daemon::HandedBack
                                     : read                        ? PanelView::Daemon::Standby
                                                                   : PanelView::Daemon::Running;
    const std::string key = std::to_string(static_cast<int>(daemon)) + (read ? text : std::string());
    view.daemon = daemon;
    view.haveStatus = read;
    if (read) view.status = status;
    if (key == lastText) return false;
    lastText = key;
    return true;
}

/**
 * パネルのロックを取る。取れたら自分の PID を書いて、ファイルを開いたままにする（本体はこの PID でパネルの有無を見る）。
 * @param lockFd 取れたときのファイルの書き込み先
 * @param holderPid 取れなかったときの、動いている側の PID の書き込み先（読めなければ 0）
 * @return 取れた（またはロックを使えない）なら true
 */
bool acquirePanelLock(int& lockFd, pid_t& holderPid) {
    lockFd = -1;
    holderPid = 0;
    std::string reason;
    const int fd = openLockFile(kPanelName, reason);
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
    // 動いている側がロックを取った直後で、まだ PID を書いていないことがあるので少し待って読み直す
    for (int attempt = 0; attempt < 10 && holderPid <= 0; ++attempt) {
        char buffer[32] = {};
        if (::pread(fd, buffer, sizeof(buffer) - 1, 0) > 0) holderPid = static_cast<pid_t>(std::atol(buffer));
        if (holderPid <= 0) std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
    ::close(fd);
    return false;
}

/**
 * 本体に「終了」を知らせる（SIGUSR2。本体は対応表を戻して、終了コード 3 で終わる）。
 */
void stopDaemon() {
    const pid_t pid = runningPid(kDaemonName);
    if (pid <= 0) {
        std::fprintf(stderr, "[終了] 本体は動いていません\n");
        return;
    }
    if (::kill(pid, SIGUSR2) == 0) {
        std::fprintf(stderr, "[終了] 本体（PID %d）に終了を知らせました\n", static_cast<int>(pid));
    } else {
        std::fprintf(stderr, "[終了] 本体（PID %d）に知らせを送れません: %s\n", static_cast<int>(pid), std::strerror(errno));
    }
}

/**
 * 設定を保存する（失敗したらログに書く）。
 * @param config 設定
 * @param watcher 設定ファイルの見張り役（自分の書き込みを読み直さない）
 */
void saveSettings(const Config& config, ConfigWatcher& watcher) {
    std::string error;
    if (saveConfig(watcher.path(), config, error)) {
        watcher.noteSaved();
    } else {
        std::fprintf(stderr, "[設定] 保存に失敗: %s\n", error.c_str());
    }
}

/**
 * コマンドを 1 回だけ実行する（［試す］。シェルは通さない。終わるのは待たない）。
 * ログには label と引数の数だけ（argv は FRAME_AUX_DEBUG=1 のときだけ）。
 * @param argv コマンドと引数
 * @param label ログに出す名前（コマンドの id など）
 * @return exec できたら true
 */
bool tryCommand(const std::vector<std::string>& argv, const std::string& label) {
    if (argv.empty()) return false;
    SendOscCommand osc;
    if (parseSendOscCommand(argv, osc)) {
        // 組み込みの OSC の送信（プリセット「VRChat のミュート（PC）」）はこのプロセスで送る
        const SendOscResult sent = sendOscPress(osc);
        std::fputs(tryCommandLogLine(label, argv, sent.ok, sent.error, commandDebugEnabled()).c_str(), stderr);
        return sent.ok;
    }
    std::string error;
    const bool ok = spawnDetached(argv, error);
    std::fputs(tryCommandLogLine(label, argv, ok, error, commandDebugEnabled()).c_str(), stderr);
    return ok;
}

/**
 * 選んでいるプリセットを 1 回だけ試す（［試す］）。VRChat のミュートは OSC を送り、スピーカーのミュートは起動する。
 * ログには IP を出さない（FRAME_AUX_DEBUG=1 のときだけ argv に入る）。
 * @param panel 設定パネル
 * @param language 言語（名前に使う）
 * @return 結果
 */
SettingsPanel::PresetTestResult tryPreset(const SettingsPanel& panel, Language language) {
    using R = SettingsPanel::PresetTestResult;
    std::string name;
    std::vector<std::string> argv;
    if (!panel.presetCommand(language, name, argv)) return R::Failed;
    SendOscCommand osc;
    if (parseSendOscCommand(argv, osc)) {
        const SendOscResult sent = sendOscPress(osc);
        std::fputs(tryCommandLogLine("プリセット（VRChat のミュート）", argv, sent.ok, sent.error, commandDebugEnabled()).c_str(),
                   stderr);
        if (sent.ok) return R::Sent;
        return sent.reason == "no_pc" ? R::NoPc : R::Failed;
    }
    return tryCommand(argv, "プリセット（スピーカーのミュート）") ? R::Ran : R::Failed;
}

/**
 * 最後に選んだタブを覚える（変わったときだけ書く。書けなければ 1 回だけログに出す）。
 * @param tab 今のタブ
 * @param saved 前に書いたタブ（書けたら更新する）
 */
void rememberTab(PanelTab tab, std::string& saved) {
    const std::string name = panelTabName(tab);
    if (name == saved) return;
    static std::string lastError;
    std::string error;
    if (!savePanelTab(panelStatePath(), name, error) && error != lastError) {
        std::fprintf(stderr, "[タブ] 最後に選んだタブを書けません: %s\n", error.c_str());
        lastError = error;
    }
    saved = name;  // 書けなくても、同じタブのまま書き直し続けない
}

/**
 * 設定パネルとして常駐する。
 * @param options コマンドライン
 * @return 終了コード
 */
int runPanel(const Options& options) {
    int lockFd = -1;
    pid_t holderPid = 0;
    bool locked = acquirePanelLock(lockFd, holderPid);
    if (!locked && runningAsUnit(kPanelUnit)) {
        // サービスとして起動されたときは、手で起動したものが終わるのを待つ
        std::fprintf(stderr, "[起動] サービスの外で動いています（PID %d）。終わるまで待ちます\n", static_cast<int>(holderPid));
        while (!locked && !gStopRequested) {
            std::this_thread::sleep_for(std::chrono::seconds(2));
            locked = acquirePanelLock(lockFd, holderPid);
        }
        if (!locked) return 0;
    }
    if (!locked) {
        if (holderPid > 0 && processIs(holderPid, kPanelName) && ::kill(holderPid, SIGUSR1) == 0) {
            std::fprintf(stderr, "[起動] すでに動いています（PID %d）。そちらのタブを開いて終わります\n",
                         static_cast<int>(holderPid));
            return 0;
        }
        std::fprintf(stderr, "[起動] すでに動いているようですが、知らせを送れませんでした（PID %d）\n",
                     static_cast<int>(holderPid));
        return 1;
    }

    ConfigWatcher watcher(options.configPath);
    Config config = watcher.loadInitial();
    FontSet fonts;
    fonts.load(kFontRegular, kFontBold);
    SettingsPanel panel(fonts);
    // 最初のタブ: --tab があればそれ、無ければ最後に選んだタブ（読めない・知らない名前なら割り当て）
    std::string savedTab;
    if (options.tabGiven) {
        panel.setTab(parseTab(options.tab));
    } else if (loadPanelTab(panelStatePath(), savedTab)) {
        PanelTab tab = PanelTab::Assign;
        if (parsePanelTab(savedTab, tab)) {
            panel.setTab(tab);
            std::fprintf(stderr, "[タブ] 最後に選んだタブ（%s）から始めます\n", savedTab.c_str());
        } else {
            savedTab.clear();
        }
    }
    Autostart autostart;
    frame_updater::UpdateChecker updater(makeUpdaterConfig());
    std::uint64_t lastUpdaterRevision = updater.revision();
    frame_apps::AppsManager apps(makeAppsConfig());
    apps.refreshNow();
    // 一覧はサイトから取ってくる（「新しい版の確認」がオンのときだけ。1 時間に 1 回まで。取れなければ前回の分 → 同梱の一覧）
    apps.setFetchEnabled(config.updateCheck);
    if (apps.fetchIfDue()) std::fprintf(stderr, "[アプリ] サイトから一覧を取りに行きます\n");
    bool appsListWasOpen = false;
    std::string lastAppsError;
    DashboardOverlay vr;
    // キーボードの方式: 既定は Minimal（1 文字ずつ組み立てる）。--keyboard-buffered か
    // FRAME_AUX_KEYBOARD=buffered で今までの方式（実機で比べるため）
    const char* keyboardEnv = std::getenv("FRAME_AUX_KEYBOARD");
    const bool buffered = options.keyboardBuffered || (keyboardEnv != nullptr && std::string(keyboardEnv) == "buffered");
    vr.setKeyboardMinimal(!buffered);
    // FRAME_AUX_KEYBOARD_MODAL=1 で KeyboardFlag_Modal を付ける（パネルを押すたびに閉じる、前の動き。実機で比べるため）
    const char* modalEnv = std::getenv("FRAME_AUX_KEYBOARD_MODAL");
    const bool modal = modalEnv != nullptr && std::string(modalEnv) == "1";
    vr.setKeyboardModal(modal);
    std::fprintf(stderr, "[キーボード] 方式: %s、Modal %s\n", buffered ? "buffered（今までの方式）" : "minimal（1 文字ずつ組み立てる）",
                 modal ? "あり（FRAME_AUX_KEYBOARD_MODAL=1）" : "なし");
    PanelView view;
    std::string lastStatusKey;
    readDaemonState(view, lastStatusKey);

    // 本体が止まっていれば起動する（パネルを開いた = アプリを使う）
    if (view.daemon == PanelView::Daemon::Stopped) autostart.startDaemon();

    std::string lastMessage;
    while (!gStopRequested) {
        std::string message;
        const DashboardOverlay::ConnectResult result =
            vr.connect(SettingsPanel::kWidth, SettingsPanel::kHeight, message);
        if (result == DashboardOverlay::ConnectResult::Ok) break;
        if (message != lastMessage) {
            std::fprintf(stderr, "[VR] %s（3 秒おきに再試行）\n",
                         result == DashboardOverlay::ConnectResult::NotRunning ? "SteamVR が起動していないので待ちます"
                                                                               : ("接続に失敗: " + message).c_str());
            lastMessage = message;
        }
        gShowRequested = 0;
        sleepInterruptible(3.0);
    }
    if (gStopRequested) {
        if (lockFd >= 0) ::close(lockFd);
        return 0;
    }
    std::fprintf(stderr, "[VR] SteamVR につながりました\n");
    {
        std::vector<uint8_t> thumbnail;
        loadThumbnail(fonts, kThumbnailSize, thumbnail);
        vr.submitThumbnail(thumbnail.data(), kThumbnailSize);
    }

    bool dirty = true;
    bool visible = false;
    bool wasVisible = false;
    bool userQuit = false;
    double nextSlowCheck = 0.0;
    double lastAutostartRefresh = 0.0;
    double lastPointerEvent = -1e9;
    double lastLoop = nowSeconds();
    double closeFieldAt = 0;  // この時刻を過ぎたら、入力中の目印を消す（キーボードが閉じたあと。0 なら予定なし）
    while (!gStopRequested && !userQuit) {
        const double now = nowSeconds();
        if (gShowRequested) {
            gShowRequested = 0;
            std::fprintf(stderr, "[起動] 2 つ目の起動から知らせが来たので、ダッシュボードのこのタブを開きます\n");
            vr.showSettings();
        }
        const bool slowCheck = now >= nextSlowCheck;
        if (slowCheck) {
            nextSlowCheck = now + kSlowCheckSec;
            if (watcher.reloadIfChanged(config)) dirty = true;
            updater.tick(config.updateCheck);
            if (updater.revision() != lastUpdaterRevision) {
                lastUpdaterRevision = updater.revision();
                dirty = true;
            }
            dirty |= readDaemonState(view, lastStatusKey);
            // コマンドのフォルダの実行ファイル（増減に追いつく）
            std::vector<CommandDef> folder = scanCommandFolder(config.commandFolder);
            if (!(folder == view.folderCommands)) {
                view.folderCommands = folder;
                dirty = true;
            }
            // アプリタブを見ている間は、ほかのアプリが入っているか・動いているかを数秒おきに見直す
            if (visible && panel.appsShown()) apps.refreshIfStale(kAppsRefreshSec);
            // 追加のシートで VRChat のミュート（送り先は自動）を選んでいる間は、送り先の PC を探して見せる
            if (panel.wantsPeer()) {
                std::string peer;
                const bool found = findSteamLinkPeer(peer);
                dirty |= panel.setDetectedPeer(found, peer);
            }
        }
        // 開いた Konsole が閉じた・systemctl が答えた
        apps.setFetchEnabled(config.updateCheck);
        if (apps.tick()) dirty = true;
        if (apps.lastError() != lastAppsError) {
            lastAppsError = apps.lastError();
            if (!lastAppsError.empty()) std::fprintf(stderr, "[アプリ] 一覧: %s\n", lastAppsError.c_str());
        }
        // 一覧を開いたら、古ければ取りに行く
        if (panel.appsListOpen() && !appsListWasOpen && apps.fetchIfDue()) {
            std::fprintf(stderr, "[アプリ] 一覧を開いたので、サイトから取りに行きます\n");
        }
        appsListWasOpen = panel.appsListOpen();
        // キーボードを開いている間も、入力をすぐ欄に出すため細かく見る
        const DashboardEvents events =
            slowCheck || visible || vr.keyboardOpen() ? vr.pollEvents(slowCheck) : DashboardEvents {};
        if (events.quit) break;
        if (events.closeRequested) {
            // ダッシュボードの「閉じる」はパネルだけを閉じる（本体は run_in_background に従う）
            std::fprintf(stderr, "[VR] ダッシュボードの「閉じる」でパネルを閉じます\n");
            userQuit = true;
            break;
        }
        if (slowCheck && !vr.steamVrAlive()) {
            std::fprintf(stderr, "[VR] vrserver がいなくなったので終了します\n");
            break;
        }
        if (!events.pointer.empty()) lastPointerEvent = now;
        /**
         * SteamVR のキーボードの結果をシートの欄に入れる。
         * @param keyboard pollEvents か hideKeyboard の結果
         */
        const auto applyKeyboard = [&](const DashboardEvents& keyboard) {
            if (keyboard.keyboardChanged && panel.sheetOpen()) {
                panel.setSheetText(static_cast<int>(keyboard.keyboardField), keyboard.keyboardText, keyboard.keyboardCursor);
                dirty = true;
            }
            if (keyboard.keyboardFinished) {
                // キーボードが閉じたら、入力中の目印とカーソルを消す。ただし閉じてすぐ開き直すこともあるので
                // （Modal のときは欄を押した瞬間に SteamVR が閉じる）、300ms は目印を残す（その間に開き直したら続けて見せる）
                closeFieldAt = now + kFieldCloseGraceSec;
                dirty = true;
            }
        };
        applyKeyboard(events);
        if (closeFieldAt > 0 && now >= closeFieldAt && !vr.keyboardOpen()) {
            panel.setActiveField(-1);
            closeFieldAt = 0;
            dirty = true;
        }
        // カーソルの点滅（キーボードを開いている間だけ描き直す）
        if (vr.keyboardOpen()) dirty |= panel.caretTick(now);
        for (const PointerInput& input : events.pointer) {
            switch (input.type) {
                case PointerInput::Type::Move: dirty |= panel.pointerMove(input.x, input.y); break;
                case PointerInput::Type::Down: {
                    const PanelAction action = panel.pointerDown(input.x, input.y, nowSeconds());
                    using K = PanelAction::Kind;
                    if (action.kind == K::Quit) {
                        std::fprintf(stderr, "[VR] 「終了」で本体とパネルを終了します\n");
                        stopDaemon();
                        userQuit = true;
                    } else if (action.kind == K::AutostartOn || action.kind == K::AutostartOff) {
                        autostart.request(action.kind == K::AutostartOn);
                    } else if (action.kind == K::UpdateCheckNow) {
                        updater.checkNow();
                    } else if (action.kind == K::UpdateConfirmYes || action.kind == K::UpdateRetry) {
                        updater.install();
                    } else if (action.kind == K::UpdateDismiss) {
                        updater.dismiss();
                    } else if (action.kind == K::CommandTest) {
                        const CommandDef* command = findCommand(config.commands, view.folderCommands, action.text);
                        panel.setRowTestResult(action.text, command != nullptr && tryCommand(command->argv, "id " + command->id));
                    } else if (action.kind == K::SheetName || action.kind == K::SheetCommand || action.kind == K::SheetIp) {
                        // 欄を押したら SteamVR のキーボード（日本語は frame-jp-keyboard が Steam のキーボードに重なる）
                        const UiText& t = uiText(config.language);
                        // 押した位置（無ければ末尾）を入力位置にして、キーボードを開く（開き直す）。初期値は欄の今の文字
                        // （直前の Closed で欄に確定した文字も入っている）
                        size_t tapCursor = std::string::npos;
                        if (!panel.takeTapCursor(tapCursor)) tapCursor = std::string::npos;
                        const uint64_t field = action.kind == K::SheetName      ? SettingsPanel::kFieldName
                                               : action.kind == K::SheetCommand ? SettingsPanel::kFieldCommand
                                                                                : SettingsPanel::kFieldIp;
                        const std::string existing = panel.sheetFieldText(static_cast<int>(field));
                        const char* description = field == SettingsPanel::kFieldName      ? t.keyboardName
                                                  : field == SettingsPanel::kFieldCommand ? t.keyboardCommand
                                                                                          : t.keyboardIp;
                        if (vr.keyboardOpen() && vr.keyboardField() == field) {
                            // 開いている間に同じ欄: 開き直さずに入力位置だけ移す（今までの方式では位置は Steam の中なので、そのまま）
                            if (vr.keyboardMinimal()) {
                                vr.setKeyboardCursor(tapCursor);
                                panel.setActiveField(static_cast<int>(field), vr.keyboardCursor());
                                std::fprintf(stderr, "[キーボード] 同じ欄を押したので、入力位置だけ %zu に移します\n",
                                             vr.keyboardCursor());
                            }
                        } else if (vr.keyboardOpen() && vr.retargetKeyboard(field, existing, tapCursor)) {
                            // 開いている間に別の欄: 開き直さずに入力先を切り替える
                            panel.setActiveField(static_cast<int>(field), vr.keyboardCursor());
                            closeFieldAt = 0;
                        } else {
                            // 閉じている（または切り替えられない）: その欄で開く。開いていたら、ためた文字を確定してから閉じる
                            if (vr.keyboardOpen()) applyKeyboard(vr.hideKeyboard("別の欄で開き直す"));
                            if (vr.showKeyboard(field, description, existing, tapCursor)) {
                                panel.setActiveField(static_cast<int>(field), tapCursor);
                                closeFieldAt = 0;  // 閉じた直後に開き直した: 目印は消さずに続ける
                            }
                        }
                    } else if (action.kind == K::SheetTest) {
                        std::vector<std::string> argv;
                        std::string error;
                        panel.setSheetTestResult(splitCommandLine(panel.sheetCommand(), argv, error) &&
                                                     tryCommand(argv, panel.sheetId().empty() ? std::string("編集中のコマンド（新規）")
                                                                                              : "編集中のコマンド（id " + panel.sheetId() + "）"));
                    } else if (action.kind == K::AppsConfirmLaunch) {
                        // ほかのアプリを入れる: Konsole でインストーラーを開く（中身は手で打ったときと同じ）
                        const frame_apps::LaunchResult result = apps.openInstaller(
                            action.text, config.language == Language::Ja ? frame_apps::Lang::Ja : frame_apps::Lang::En);
                        panel.setAppsLaunchResult(result);
                        std::fprintf(stderr, "[アプリ] Konsole でインストーラーを%s（%s）\n",
                                     result == frame_apps::LaunchResult::Started ? "開きました" : "開けませんでした",
                                     action.text.empty() ? "メニュー" : action.text.c_str());
                    } else if (action.kind == K::PresetTest) {
                        panel.setPresetTestResult(tryPreset(panel, config.language));
                    } else if (action.kind == K::PresetAdd) {
                        // 選んだプリセットを、ふつうのコマンドとして登録する
                        if (vr.keyboardOpen()) applyKeyboard(vr.hideKeyboard("追加"));
                        std::string name;
                        std::vector<std::string> argv;
                        std::string savedId;
                        std::string error = "IP アドレスの形ではありません";
                        if (panel.presetCommand(config.language, name, argv) &&
                            saveCommand(config, "", name, joinCommandLine(argv), savedId, error)) {
                            saveSettings(config, watcher);
                            panel.onCommandSaved(savedId, config);
                            std::fprintf(stderr, "[コマンド] プリセットを id %s で追加しました\n", savedId.c_str());
                        } else {
                            std::fprintf(stderr, "[コマンド] プリセットを追加できません: %s\n", error.c_str());
                        }
                    } else if (action.kind == K::SheetSave) {
                        // 保存の前に、キーボードでためた文字を欄に確定して閉じる
                        if (vr.keyboardOpen()) applyKeyboard(vr.hideKeyboard("保存"));
                        std::string savedId;
                        std::string error;
                        if (saveCommand(config, panel.sheetId(), panel.sheetName(), panel.sheetCommand(), savedId, error)) {
                            saveSettings(config, watcher);
                            panel.onCommandSaved(savedId, config);
                        } else {
                            std::fprintf(stderr, "[コマンド] 保存できません: %s\n", error.c_str());
                        }
                    } else if (applyPanelAction(action, config)) {
                        saveSettings(config, watcher);
                    }
                    dirty = true;
                    break;
                }
                case PointerInput::Type::Up: dirty |= panel.pointerUp(); break;
                case PointerInput::Type::Leave: dirty |= panel.pointerLeave(); break;
                case PointerInput::Type::Scroll: dirty |= panel.scroll(input.y); break;
            }
        }
        // つまみで並び替えている間は、端の近くで自動でスクロールする。離したら、その並びを保存する
        if (panel.dragging()) dirty |= panel.dragTick(std::fmin(0.1, now - lastLoop));
        lastLoop = now;
        std::vector<std::string> order;
        if (panel.takeReorder(order) && setCommandOrder(config, view.folderCommands, order)) {
            saveSettings(config, watcher);
            dirty = true;
        }
        // Modal でないキーボードは勝手に閉じないので、シートを閉じた・やめた・タブを替えた・欄が見えなくなった
        // （プリセットと自分で書くを切り替えた）ら自分で閉じる
        if (vr.keyboardOpen() && !panel.sheetFieldShown(static_cast<int>(vr.keyboardField()))) {
            applyKeyboard(vr.hideKeyboard(!panel.sheetOpen()                    ? "シートを閉じた"
                                          : panel.tab() != PanelTab::Commands ? "タブを替えた"
                                                                              : "欄が見えなくなった"));
        }
        // 最後に選んだタブを覚える（パネルが落ちても、次に開いたときそのタブから）
        rememberTab(panel.tab(), savedTab);
        if (userQuit) break;
        if (slowCheck) {
            dirty |= panel.tick(now);
            dirty |= autostart.poll();
            visible = vr.settingsVisible();
            if (visible != wasVisible) {
                std::fprintf(stderr, "[設定パネル] %s\n", visible ? "見えました" : "隠れました");
            }
            // パネルが隠れた（ダッシュボードを閉じた）ら、キーボードも閉じる（文字は欄に確定する）
            if (!visible && vr.keyboardOpen()) applyKeyboard(vr.hideKeyboard("パネルが隠れた"));
        }
        if (visible && slowCheck && (!wasVisible || now - lastAutostartRefresh >= kAutostartRefreshSec)) {
            lastAutostartRefresh = now;
            dirty |= autostart.refresh();
        }
        if (visible && (dirty || !wasVisible)) {
            view.autostart = autostart.status();
            view.update = updater.status();
            view.apps = apps.entries();
            view.menuBusy = apps.busy("");
            panel.render(config, view);
            vr.submitSettings(panel.toRgba().data());
            dirty = false;
        }
        wasVisible = visible;

        // 待つ: 見えている間はマウスに素早く応えるため 50〜100ms、それ以外は次の確認まで
        const double t = nowSeconds();
        const double pollSec = panel.dragging() || vr.keyboardOpen() || closeFieldAt > 0 || t - lastPointerEvent < kPointerHoldSec
                                   ? kPointerPollSec
                                   : kVisiblePollSec;
        const double untilSlow = std::fmax(0.0, nextSlowCheck - t);
        sleepInterruptible(visible || vr.keyboardOpen() || closeFieldAt > 0 ? std::fmin(untilSlow, pollSec) : untilSlow);
    }

    vr.hideKeyboard("終了");
    vr.shutdown();
    std::fprintf(stderr, "[VR] 終了しました\n");
    if (lockFd >= 0) ::close(lockFd);
    return userQuit ? kExitCodeUserQuit : 0;
}

}  // namespace

/**
 * エントリーポイント。
 * @param argc 引数の数
 * @param argv 引数
 * @return 終了コード
 */
int main(int argc, char** argv) {
    std::setvbuf(stderr, nullptr, _IOLBF, 0);
    installSignalHandlers();
    Options options;
    if (!parseOptions(argc, argv, options)) {
        printUsage();
        return 2;
    }
    switch (options.mode) {
        case Options::Mode::Help: printUsage(); return 0;
        case Options::Mode::Version:
            std::printf("frame-aux-shortcuts-panel %s\n", FRAME_AUX_SHORTCUTS_VERSION);
            return 0;
        case Options::Mode::DumpPng: return runDumpPng(options);
        case Options::Mode::Panel: break;
    }
    return runPanel(options);
}
