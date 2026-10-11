// アクションの実行の実装。
// 書き込むのは: PipeWire のミュート（wpctl）、SteamVR の設定（IVRSettings）、frame-perf-overlay へのシグナル、
// SteamVR へのメールボックスの送信、スクリーンショットの保存先のフォルダ（PNG は vrcompositor が書く）、
// 組み込みのコマンドの OSC（つないでいる PC への UDP）、aux の元の動きの入力（gpio-keys への write。keymap.h）だけ。
// sysfs・/opt・/etc には触らない。PipeWire・WirePlumber の再起動もしない。トラッキング用のカメラも開かない。
#include "actions.h"

#include "brightness.h"
#include "command.h"
#include "commands.h"
#include "keymap.h"
#include "osc.h"
#include "room_view.h"
#include "runtime.h"
#include "screenshot.h"
#include "vr_client.h"

#include <signal.h>
#include <sys/stat.h>
#include <unistd.h>

#include <cerrno>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>

namespace {

/**
 * 数値を短い文字列にする（ログ用）。
 * @param value 値
 * @param decimals 小数点以下の桁数
 * @return 文字列
 */
std::string fmt(double value, int decimals) {
    char buffer[32];
    std::snprintf(buffer, sizeof(buffer), "%.*f", decimals, value);
    return buffer;
}

/**
 * 失敗の結果を作る。
 * @param reason 理由のコード
 * @param detail ログ用の説明
 * @return 結果
 */
ActionOutcome failure(const std::string& reason, const std::string& detail) {
    ActionOutcome out;
    out.reason = reason;
    out.detail = detail;
    return out;
}

/**
 * マイクのミュートを切り替える。終わったら今のミュート状態を読んで結果に入れる。
 * @return 結果
 */
ActionOutcome micMuteToggle() {
    const std::vector<std::string> argv = {"wpctl", "set-mute", "@DEFAULT_AUDIO_SOURCE@", "toggle"};
    const CommandResult result = runCommand(argv);
    ActionOutcome out;
    out.detail = describeCommand(argv, result);
    if (!result.ok()) {
        out.reason = "failed";
        return out;
    }
    out.ok = true;
    // 結果の確認（読めなくても切り替えは成功扱い）。"Volume: 1.00 [MUTED]" の形
    const CommandResult volume = runCommand({"wpctl", "get-volume", "@DEFAULT_AUDIO_SOURCE@"});
    bool muted = false;
    if (volume.ok() && parseWpctlMuted(volume.out, muted)) {
        out.value = muted ? "muted" : "unmuted";
        out.detail += muted ? "（ミュート中）" : "（ミュート解除）";
    }
    return out;
}

/**
 * frame-perf-overlay の表示を切り替える（ロックファイルの PID に SIGUSR1）。
 * 送る前に、その PID が本当に frame-perf-overlay かを /proc で確かめる（PID の使い回しで別のプロセスに送らない）。
 * 結果の値は、送る前の表示（設定ファイルの visible）の反対。
 * @return 結果
 */
ActionOutcome perfOverlayToggle() {
    const pid_t pid = runningPid(kPerfOverlayName);
    if (pid <= 0) {
        if (!isInstalled(kPerfOverlayName)) return failure("app_missing", "frame-perf-overlay が入っていません");
        return failure("app_stopped", "frame-perf-overlay が動いていません（ロックファイルの PID がいない）");
    }
    const int visible = readPerfOverlayVisible();
    if (::kill(pid, SIGUSR1) != 0) {
        return failure("failed", "PID " + std::to_string(pid) + " に SIGUSR1 を送れません: " + std::strerror(errno));
    }
    ActionOutcome out;
    out.ok = true;
    if (visible >= 0) out.value = visible ? "hidden" : "shown";
    out.detail = "frame-perf-overlay（PID " + std::to_string(pid) + "）に表示の切り替えを送りました";
    return out;
}

/**
 * 明るさを段の順に切り替える。今の analogGain に一番近い段の次へ。
 * @param vr SteamVR とのつながり
 * @param levels 段（スライダーの %）
 * @return 結果（値は新しい %）
 */
ActionOutcome brightnessCycle(VrClient& vr, const std::vector<double>& levels) {
    if (!vr.connected()) return failure("no_steamvr", "SteamVR につながっていません");
    if (levels.empty()) return failure("failed", "明るさの段がありません");
    float current = 0.0f;
    std::string error;
    if (!vr.getFloat("steamvr", "analogGain", current, error)) return failure("failed", "今の明るさを読めません: " + error);
    const size_t next = nextBrightnessIndex(levels, current);
    const double gain = brightnessPercentToGain(levels[next]);
    if (!vr.setFloat("steamvr", "analogGain", static_cast<float>(gain), error)) {
        return failure("failed", "明るさを書けません: " + error);
    }
    ActionOutcome out;
    out.ok = true;
    out.value = fmt(levels[next], 0);
    out.detail = "明るさ " + fmt(current, 3) + " → " + fmt(levels[next], 0) + "%（" + fmt(gain, 3) + "）";
    return out;
}

/**
 * room view のオン・オフ。今の状態を判断して、反対を作業用スレッドで送る。
 * 今の状態は、アプリ（ゲーム）が動いていれば VREvent_RoomViewShown / Hidden で覚えた状態、
 * 動いていない（ホーム画面）か、まだイベントが来ていなければ設定 camera/enableConstructRoomView。
 * 非公開の仕組みを使う（room_view.h を参照）。
 * @param vr SteamVR とのつながり
 * @param out 結果の書き込み先
 */
void prepareRoomViewToggle(VrClient& vr, PreparedAction& out) {
    if (!vr.connected()) {
        out.done = true;
        out.outcome = failure("no_steamvr", "SteamVR につながっていません");
        return;
    }
    vr.pollEvents();  // 直前に来ていた表示・非表示のイベントを反映する
    bool current = false;
    bool remembered = false;
    std::string source;
    if (vr.sceneProcessId() != 0 && vr.knownRoomView(remembered)) {
        current = remembered;
        source = "イベント";
    } else {
        std::string error;
        if (!vr.getBool("camera", "enableConstructRoomView", current, error)) {
            out.done = true;
            out.outcome = failure("failed", "今の room view の状態を読めません: " + error);
            return;
        }
        source = "設定";
    }
    const bool target = !current;
    out.deferred = [target, current, source]() {
        std::string error;
        const bool ok = sendRoomViewEnabled(target, error);
        ActionOutcome result;
        result.ok = ok;
        result.value = target ? "on" : "off";
        if (!ok) result.reason = "room_view_api";
        result.detail = std::string("room view ") + (current ? "オン" : "オフ") + "（" + source + "）→ " +
                        (target ? "オン" : "オフ") + (ok ? " を送りました" : " を送れません: " + error);
        return result;
    };
}

/**
 * スクリーンショットを頼む（OpenVR の IVRScreenshots。VR の画面そのものが撮れる）。結果は SteamVR のイベントで来る。
 * @param vr SteamVR とのつながり
 * @param dir 保存先のフォルダ
 * @param out 結果の書き込み先
 */
void prepareScreenshot(VrClient& vr, const std::string& dir, PreparedAction& out) {
    if (!vr.connected()) {
        out.done = true;
        out.outcome = failure("no_steamvr", "SteamVR につながっていません");
        return;
    }
    const std::time_t now = std::time(nullptr);
    std::tm tm {};
    localtime_r(&now, &tm);
    ScreenshotPaths paths;
    std::string error;
    if (!prepareScreenshotPaths(dir, tm, paths, error)) {
        out.done = true;
        out.outcome = failure("failed", error);
        return;
    }
    uint32_t handle = 0;
    if (!vr.requestScreenshot(paths.previewBase, paths.vrBase, handle, error)) {
        out.done = true;
        out.outcome = failure("failed", error);
        return;
    }
    out.screenshot = true;
    out.shot.handle = handle;
    out.shot.path = paths.path;
    out.shot.vrBase = paths.vrBase;
}

/**
 * VR 用のファイル（Mono では書かれないはず）が書かれていたら消す。
 * @param shot 頼んだもの
 */
void removeVrFile(const PendingScreenshot& shot) {
    for (const char* ext : {".png", ".jpg", ""}) ::unlink((shot.vrBase + ext).c_str());
}

}  // namespace

ActionOutcome finishScreenshot(VrClient& vr, const PendingScreenshot& shot, bool eventOk, bool toSteam) {
    removeVrFile(shot);
    struct stat st {};
    const bool exists = ::stat(shot.path.c_str(), &st) == 0 && st.st_size > 0;
    if (!eventOk) return failure("failed", "SteamVR がスクリーンショットに失敗しました（VREvent_ScreenshotFailed）");
    if (!exists) return failure("failed", shot.path + " ができていません（VREvent_ScreenshotTaken は来た）");
    ActionOutcome out;
    out.ok = true;
    out.value = shot.path;
    out.detail = shot.path + " に保存しました";
    if (toSteam) {
        std::string error;
        out.detail += vr.submitScreenshot(shot.handle, shot.path, error) ? "（Steam に渡しました）"
                                                                         : "（Steam には渡せません: " + error + "）";
    }
    return out;
}

ActionOutcome screenshotTimedOut(const PendingScreenshot& shot) {
    removeVrFile(shot);
    return failure("failed", "スクリーンショットの結果が 5 秒たっても来ません（" + shot.path + "）");
}

PreparedAction prepareAction(const Action& action, const Config& config, VrClient& vr) {
    PreparedAction out;
    switch (action.type) {
        case ActionType::None:
            out.done = true;
            out.outcome.ok = true;
            out.outcome.detail = "何もしない割り当てです";
            break;
        case ActionType::MicMuteToggle: out.deferred = micMuteToggle; break;
        case ActionType::BrightnessCycle:
            out.done = true;
            out.outcome = brightnessCycle(vr, config.brightnessLevels);
            break;
        case ActionType::RoomViewToggle: prepareRoomViewToggle(vr, out); break;
        case ActionType::Screenshot: prepareScreenshot(vr, config.screenshotDir, out); break;
        case ActionType::PerfOverlayToggle:
            out.done = true;
            out.outcome = perfOverlayToggle();
            break;
        case ActionType::DashboardOpen: {
            out.done = true;
            std::string error;
            out.outcome.ok = vr.showDashboard(error);
            if (!out.outcome.ok) out.outcome.reason = vr.connected() ? "failed" : "no_steamvr";
            out.outcome.detail = out.outcome.ok ? "ダッシュボードを開くよう頼みました" : "ダッシュボードを開けません: " + error;
            break;
        }
        case ActionType::OriginalAux: {
            // 置き換えの一時的な戻しがあるので、メインスレッド（置き換え・戻しと同じスレッド）でその場で行う（約 80 ms 止まる）
            out.done = true;
            std::string message;
            out.outcome.ok = keymap_guard::pressOriginal(message);
            if (!out.outcome.ok) out.outcome.reason = "failed";
            out.outcome.detail = "aux の元の動き: " + message;
            break;
        }
        case ActionType::Command: {
            // 登録したコマンドか、フォルダの実行ファイル（フォルダは実行するたびに見る。増減に追いつく）
            out.done = true;
            const std::vector<CommandDef> folder = scanCommandFolder(config.commandFolder);
            const CommandDef* command = findCommand(config.commands, folder, action.commandId);
            if (command == nullptr) {
                out.outcome = failure("no_command", "コマンド " + action.commandId + " がありません");
                break;
            }
            SendOscCommand osc;
            if (parseSendOscCommand(command->argv, osc)) {
                // 組み込みの OSC の送信（プリセット「VRChat のミュート（PC）」）: 別のプロセスにせず作業用スレッドで送り、
                // 送れたか・PC が見つかったかを音とお知らせで返す
                out.done = false;
                const std::string name = command->name;
                const std::string id = command->id;
                out.deferred = [osc, name, id]() {
                    const SendOscResult sent = sendOscPress(osc);
                    ActionOutcome result;
                    result.ok = sent.ok;
                    result.value = kOscValuePrefix + osc.address;
                    result.reason = sent.reason;
                    result.detail = name + "（id " + id + "）: OSC " + (sent.ok ? "を送りました" : "を送れません: " + sent.error);
                    // アドレスと送り先は FRAME_AUX_DEBUG のときだけ（ログにコマンドの中身を書かない方針）
                    if (commandDebugEnabled()) {
                        result.detail += " ［" + osc.address + (sent.host.empty() ? "" : " → " + sent.host + ":" + std::to_string(osc.port)) + "］";
                    }
                    return result;
                };
                break;
            }
            std::string error;
            out.outcome.ok = spawnDetached(command->argv, error);
            out.outcome.value = command->name;
            if (!out.outcome.ok) out.outcome.reason = "failed";
            // ジャーナルと状態ファイルには名前と id だけ（argv は FRAME_AUX_DEBUG=1 のときだけ）
            out.outcome.detail = commandLogDetail(command->name, command->id, command->argv, out.outcome.ok, error,
                                                  commandDebugEnabled());
            break;
        }
    }
    return out;
}

bool actionNeedsVr(ActionType type) {
    return actionSpec(type).needsVr;
}
