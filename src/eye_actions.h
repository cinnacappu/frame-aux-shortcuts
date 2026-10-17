// 目の正面合わせ・目のフィット・普段の目を覚え直す（frameeyeosc の設定パネル frameeyeosc-panel に --action で頼む）。
// frameeyeosc-panel を探して --action に対応しているかを確かめること、--action の終了コードを結果にすること、目のフィットを
// もう一度押したら止めること（切り替え）をここで決める。コマンドの起動は呼ぶ側から渡せるので、単体テストできる。
// frameeyeosc-panel を起動するのは --version と --action だけ（パネル自体を起動することはない）。
#pragma once

#include "actions.h"
#include "command.h"

#include <condition_variable>
#include <functional>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

/** frameeyeosc の設定パネルの実行ファイルの名前。 */
constexpr const char* kEyePanelName = "frameeyeosc-panel";
/**
 * 対応しているかを確かめるときに頼む、無いアクションの名前。--action に対応した frameeyeosc-panel は、ソケットにつなぐ前に
 * 終了コード 1 と kEyeUnknownActionText ではねる。古い版は --action を知らない引数として別のコード（0.7.9 は 2）で終わる。
 */
constexpr const char* kEyeProbeAction = "bogus";
/** 対応した frameeyeosc-panel が知らないアクションのときに標準エラーに書く言葉（"bogus: unknown action (use …)"）。 */
constexpr const char* kEyeUnknownActionText = "unknown action";
/**
 * --action の答えを待つ上限（ms）。frameeyeosc-panel はふだん 0.25 秒以内に答え、パネルが答えなければ 2 秒であきらめて
 * 3 で終わるので、それより少し長く。フィットの途中に押したときは、このあと --action stop でもう 1 回まで待つ。
 */
constexpr int kEyeActionTimeoutMs = 3000;
/** 対応しているかの確かめと --version を待つ上限（ms）。 */
constexpr int kEyeProbeTimeoutMs = 2000;
/** 対応しているかの確かめが時間切れ・起動できなかったとき、次に確かめ直すまでの間（ms）。 */
constexpr int kEyeProbeRetryMs = 5000;

/**
 * frameeyeosc-panel の --action の終了コード。読み方（理由のコード・フィットを止めるか）は eye_actions.cpp の
 * kEyeExitRules の表 1 つにまとめてある。コードを足すときは、ここに 1 行と表に 1 行。表に無いコードは「うまくいきませんでした」。
 */
namespace eye_exit {
constexpr int kAccepted = 0;     ///< 受け付けた
constexpr int kUsage = 1;        ///< 書き方が違う（知らない引数も。知らないアクションはパネルにつなぐ前にはねる）
constexpr int kNoPanel = 2;      ///< パネルが動いていない（SteamVR を待っている間も）
constexpr int kNoAnswer = 3;     ///< パネルが答えない
constexpr int kNotRunning = 4;   ///< frameeyeosc が動いていない
constexpr int kNotFitted = 5;    ///< 目のフィットをまだしていないので、正面合わせできない
constexpr int kFitBusy = 6;      ///< フィットの途中（eye_fit なら止める）
constexpr int kNothingToStop = 7;  ///< 止めるものが無い
constexpr int kLocked = 8;       ///< フィットの値が frameeyeosc のコマンドラインで固定されている
constexpr int kNoEyeData = 9;    ///< 目のデータが来ない（ヘッドセットをかぶっていない）
constexpr int kRecenterBusy = 10;  ///< 正面合わせの途中（2.5 秒ほどで終わるので、eye_fit でも止めない）
constexpr int kNoCameras = 11;   ///< relearn: 目のカメラが使えない（まぶたにカメラを使っていない・eyecam が動いていない・校正中など）
// exec できなかった 127 は、runCommand が SpawnFailed にするので終了コードとしては届かない（入っていない側にする）
}  // namespace eye_exit

/** 目のアクションの結果の値（ActionOutcome::value・PressRecord::value）。 */
constexpr const char* kEyeRecentering = "recentering";  ///< 正面合わせを始めた
constexpr const char* kEyeFitting = "fitting";          ///< フィットを始めた
constexpr const char* kEyeRelearning = "relearning";    ///< 普段の目を覚え直し始めた
constexpr const char* kEyeStopped = "stopped";          ///< 途中のフィット（か正面合わせ）を止めた（失敗のときは止めようとした）

/** 失敗の理由のコード（ActionOutcome::reason）。 */
constexpr const char* kReasonEyeMissing = "eye_missing";              ///< frameeyeosc が入っていない
constexpr const char* kReasonEyeOutdated = "eye_outdated";            ///< frameeyeosc が古い（--action が無い）
constexpr const char* kReasonEyePanelStopped = "eye_panel_stopped";   ///< frameeyeosc のパネルが動いていない
constexpr const char* kReasonEyeNoAnswer = "eye_no_answer";           ///< frameeyeosc のパネルが答えない
constexpr const char* kReasonEyeNotRunning = "eye_not_running";       ///< frameeyeosc が動いていない
constexpr const char* kReasonEyeNotFitted = "eye_not_fitted";         ///< 目のフィットをまだしていない
constexpr const char* kReasonEyeBusy = "eye_busy";                    ///< フィットの途中
constexpr const char* kReasonEyeRecenterBusy = "eye_recenter_busy";   ///< 正面合わせの途中
constexpr const char* kReasonEyeNoCameras = "eye_no_cameras";         ///< 目のカメラが使えない（11）
constexpr const char* kReasonEyeCamerasCalibrating = "eye_cameras_calibrating";  ///< 11 のうち、目のカメラの校正中
constexpr const char* kReasonEyeCamerasNoVideo = "eye_cameras_no_video";         ///< 11 のうち、目のカメラの映像が来ていない
// 目のデータが来ない（9）は、視点のリセットと同じ kReasonNotWorn（recenter.h）。文言も同じ「かぶっている間だけ使えます」
constexpr const char* kReasonEyeLocked = "eye_locked";                ///< フィットの値が frameeyeosc のコマンドラインで固定されている

/** frameeyeosc-panel に頼むこと。 */
enum class EyeCall {
    Recenter,  ///< --action recenter
    Fit,       ///< --action fit
    Stop,      ///< --action stop
    Relearn,   ///< --action relearn
};

/** frameeyeosc-panel の様子。 */
enum class EyePanelState {
    Unknown,   ///< まだ確かめていない
    Missing,   ///< 入っていない
    Outdated,  ///< 入っているが --action に対応していない（古い版）
    Ready,     ///< --action に対応している
};

/** 終了コード 1 つの読み方（kEyeExitRules の 1 行）。 */
struct EyeExitRule {
    int code;              ///< 終了コード
    const char* reason;    ///< 失敗の理由のコード（kReasonEye… か "failed"）
    bool stopsFit = false; ///< eye_fit でこれが返ったら --action stop で止める（切り替え）
    bool okForStop = false;  ///< --action stop でこれが返ったら、止めたことにする（止めるものがもう無い）
};

/** 終了コードの理由を、標準エラーの言葉で細かくする（kEyeCameraHints の 1 行。目のカメラが使えない 11 のわけ）。 */
struct EyeErrHint {
    int code;            ///< 終了コード
    const char* text;    ///< 標準エラーにこの言葉があれば（"calibrating" など）
    const char* reason;  ///< この理由のコードにする
};

/**
 * 終了コードの読み方を表から探す。
 * @param code 終了コード
 * @return 表の行（無ければ nullptr。そのときは "failed"）
 */
const EyeExitRule* findEyeExitRule(int code);

/** frameeyeosc-panel を確かめた結果。 */
struct EyePanelInfo {
    EyePanelState state = EyePanelState::Unknown;
    std::string path;     ///< 見つけた実行ファイル（入っていなければ空）
    std::string version;  ///< --version で読めた版（"0.7.9"。見せるためだけ。読めなければ空）

    bool operator==(const EyePanelInfo& o) const { return state == o.state && path == o.path && version == o.version; }
};

/** コマンドを実行する係（本番は runCommand。テストでは終了コードを返す偽物）。 */
using EyeRunner = std::function<CommandResult(const std::vector<std::string>& argv, int timeoutMs)>;

/**
 * 目のアクションか（設定パネルでは「目」の側に並ぶ）。
 * @param type 種類
 * @return eye_recenter / eye_fit / eye_relearn なら true
 */
bool isEyeAction(ActionType type);

/**
 * frameeyeosc-panel --version の出力（"frameeyeosc-panel 0.7.9"）から版を読む。
 * @param output 標準出力
 * @param version 読めたときの書き込み先（"0.7.9"）
 * @return 読めたら true
 */
bool parseEyePanelVersion(const std::string& output, std::string& version);

/**
 * --action bogus の結果から、--action に対応しているかを決める。
 * @param result frameeyeosc-panel --action bogus の結果
 * @return 終了コード 1 で、標準エラーに kEyeUnknownActionText があれば true（それ以外は古い版とみなす）
 */
bool eyePanelSupportsActions(const CommandResult& result);

/**
 * 見つかったか・対応しているかから様子を決める。
 * @param found 実行ファイルが見つかった
 * @param supportsActions --action に対応している
 * @return 見つからなければ Missing、対応していなければ Outdated、対応していれば Ready
 */
EyePanelState eyePanelStateFor(bool found, bool supportsActions);

/**
 * frameeyeosc-panel に渡す引数。
 * @param path 実行ファイル
 * @param call 頼むこと
 * @return argv（path --action recenter|fit|stop|relearn）
 */
std::vector<std::string> eyeActionArgv(const std::string& path, EyeCall call);

/**
 * 対応しているかを確かめるときの引数。
 * @param path 実行ファイル
 * @return argv（path --action bogus）
 */
std::vector<std::string> eyeProbeArgv(const std::string& path);

/**
 * --action の結果を、アクションの結果にする（値・理由のコード・ログ用の説明）。
 * @param call 頼んだこと
 * @param result 実行の結果
 * @return 結果（止めるときの 7（止めるものが無い）は、もう終わっていたので成功にする）
 */
ActionOutcome eyeOutcomeFor(EyeCall call, const CommandResult& result);

/**
 * 目のアクションを動かす（作業用スレッドで呼ぶ）。入っていない・--action に対応していないなら頼まずに失敗にする。
 * 目のフィットは、フィットの途中（6）が返ったら止める（--action stop）。うっかりの長押しの逃げ道。
 * @param type eye_recenter / eye_fit / eye_relearn
 * @param info frameeyeosc-panel を確かめた結果
 * @param run コマンドを実行する係
 * @return 結果
 */
ActionOutcome runEyeAction(ActionType type, const EyePanelInfo& info, const EyeRunner& run);

/**
 * 本番の目のアクション（作業用スレッドで呼ぶ）。frameeyeosc-panel を探して対応しているかを確かめ（実行ファイルが
 * 変わっていなければ前に確かめた結果を使う）、runCommand で頼む。
 * @param type eye_recenter / eye_fit / eye_relearn
 * @return 結果
 */
ActionOutcome runEyeAction(ActionType type);

/** 今の時刻を返す係（ms。本番は steady_clock。テストでは時間を進められる偽物）。 */
using EyeClock = std::function<long long()>;

/**
 * frameeyeosc-panel が入っているか・--action に対応しているか・版を確かめる係。実行ファイルの場所・更新時刻・大きさが
 * 前と同じなら --action bogus と --version は起動し直さない（設定パネルが毎回プロセスを起動しないように）。
 * 覚えるのは --action bogus が終わって答えたときだけ。時間切れ・起動できなかったときは覚えず、kEyeProbeRetryMs たってから
 * 次の refresh() で確かめ直す（その間は時間切れなら Unknown、起動できなければ Missing）。
 * 1 つのスレッドから使う。
 */
class EyePanelProbe {
public:
    /**
     * @param run コマンドを実行する係（空なら runCommand）
     * @param now 今の時刻を返す係（空なら steady_clock）
     */
    explicit EyePanelProbe(EyeRunner run = {}, EyeClock now = {});

    /**
     * 確かめ直す（実行ファイルが変わったときだけ --action bogus と --version を起動する）。
     * @return 結果が変わったら true
     */
    bool refresh();

    /** @return 最後に確かめた結果 */
    const EyePanelInfo& info() const { return info_; }

private:
    EyeRunner run_;
    EyeClock now_;
    EyePanelInfo info_;
    std::string key_;         ///< 前に確かめた実行ファイルの場所・更新時刻・大きさ（答えが返ったときだけ覚える）
    long long retryAtMs_ = 0;  ///< 確かめが時間切れ・起動できなかったとき、この時刻までは確かめ直さない（0 なら待たない）
};

/**
 * EyePanelProbe を別のスレッドで動かす係（設定パネル用。--action bogus と --version の待ちで描画を止めない）。
 * request() で確かめを頼み、変わった結果はメインループが take() で受け取る（作業用スレッド action_worker.h と同じやり方）。
 * スレッドはシグナルをすべて止めて（マスクして）作る（SIGTERM などはメインスレッドが受ける）。
 */
class EyePanelWatcher {
public:
    EyePanelWatcher() = default;
    EyePanelWatcher(const EyePanelWatcher&) = delete;
    EyePanelWatcher& operator=(const EyePanelWatcher&) = delete;
    ~EyePanelWatcher();

    /** スレッドを始める。 */
    void start();

    /** 確かめを頼む（前の確かめがまだ終わっていなければ、終わってからもう 1 回だけ）。 */
    void request();

    /**
     * 変わった結果を受け取る。
     * @param info 変わっていたときの書き込み先
     * @return 前に受け取ってから変わっていたら true
     */
    bool take(EyePanelInfo& info);

    /** 頼んである確かめは捨てて、動いている確かめが終わるのを待ってから止める。 */
    void stop();

private:
    /** スレッドの本体。 */
    void run();

    EyePanelProbe probe_;  ///< スレッドの中だけで使う
    std::thread thread_;
    std::mutex mutex_;
    std::condition_variable wake_;
    bool requested_ = false;
    bool stopping_ = false;
    bool changed_ = false;  ///< latest_ をまだ受け取っていない
    EyePanelInfo latest_;
};
