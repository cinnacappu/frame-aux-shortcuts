// 押し方に割り当てたアクションの実行。
// すぐ終わるもの（OpenVR の呼び出し・シグナル・コマンドの起動）はその場で、外部コマンドの終わりを待つもの
// （wpctl）と通信するもの（room view）は、イベントループを止めないよう作業用スレッドで動かす。
// スクリーンショットは頼むだけその場で行い、結果は SteamVR のイベントで受け取る（メインループが待つ）。
#pragma once

#include "config.h"

#include <cstdint>
#include <functional>
#include <string>

class VrClient;

/** アクションの結果（状態ファイル・視界の表示・ログに使う）。 */
struct ActionOutcome {
    bool ok = false;
    std::string value;   ///< 結果の値: "muted" / "unmuted"、明るさの %、"on" / "off"、"shown" / "hidden"、保存したファイル、
                         ///< コマンドの名前、組み込みの OSC の送信なら "osc:<アドレス>"
    std::string reason;  ///< 失敗の理由のコード: "no_steamvr" / "room_view_api" / "no_command" / "app_missing" / "app_stopped" /
                         ///< "no_pc"（OSC の送り先の PC が見つからない）/ "failed"
    std::string detail;  ///< ログ用の説明
};

/** 作業用スレッドで動かす処理。 */
using DeferredAction = std::function<ActionOutcome()>;

/** 頼んだスクリーンショット（vrcompositor が撮り終えると VREvent_ScreenshotTaken / Failed が来る）。 */
struct PendingScreenshot {
    uint32_t handle = 0;  ///< RequestScreenshot の番号
    std::string path;     ///< できあがるファイル
    std::string vrBase;   ///< VR 用のファイル名（拡張子なし。書かれていたら消す）
};

/** スクリーンショットの結果を待つ上限（秒）。これを過ぎたら失敗にする。 */
constexpr double kScreenshotTimeoutSec = 5.0;

/** prepareAction() の結果。 */
struct PreparedAction {
    bool done = false;         ///< その場で終わった（outcome が結果）
    ActionOutcome outcome;     ///< その場で終わったときの結果
    DeferredAction deferred;   ///< done も screenshot も false のとき、作業用スレッドで動かす処理
    bool screenshot = false;   ///< スクリーンショットを頼んだ（結果は SteamVR のイベントで来る。shot を待つ）
    PendingScreenshot shot;
};

/**
 * スクリーンショットの結果のイベントが来たときの結果を作る（ファイルができたかも確かめる）。
 * 撮れていて toSteam なら Steam にも渡す（渡せなくても、ファイルが保存できていれば成功。ログの説明に書く）。
 * @param vr SteamVR とのつながり
 * @param shot 頼んだもの
 * @param eventOk VREvent_ScreenshotTaken なら true、Failed なら false
 * @param toSteam Steam のライブラリにも入れるか（設定 screenshot_to_steam）
 * @return 結果
 */
ActionOutcome finishScreenshot(VrClient& vr, const PendingScreenshot& shot, bool eventOk, bool toSteam);

/** ミュート中の表示を隠してから、コンポジターが隠した画面を出すまで待つ時間（ms。90Hz で 5 フレームほど）。 */
constexpr int kBadgeHideWaitMs = 60;

/**
 * スクリーンショットの結果が時間内に来なかったときの結果。
 * @param shot 頼んだもの
 * @return 失敗の結果
 */
ActionOutcome screenshotTimedOut(const PendingScreenshot& shot);

/**
 * アクションを始める。すぐ終わるものはその場で実行し、待つものは作業用スレッドで動かす処理を返す。
 * OpenVR を使うもの（明るさ・ダッシュボード・room view の状態の判断）はここ（メインスレッド）で呼ぶ。
 * @param action 割り当て
 * @param config 設定（明るさの段・スクリーンショットの保存先）
 * @param vr SteamVR とのつながり（つながっていなければ、OpenVR を使うアクションは失敗）
 * @return 結果か、作業用スレッドで動かす処理
 */
PreparedAction prepareAction(const Action& action, const Config& config, VrClient& vr);

/**
 * アクションに SteamVR が要るか（--test-action でつなぐかどうか）。
 * @param type 種類
 * @return 要るなら true
 */
bool actionNeedsVr(ActionType type);
