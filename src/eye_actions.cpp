// 目の正面合わせ・目のフィット・普段の目を覚え直すの実装。
// 起動するのは frameeyeosc-panel の --version と --action だけ。frameeyeosc のファイルには触らない。
// 対応しているかの確かめ（--action bogus）は、frameeyeosc-panel がソケットにつなぐ前に名前ではねるので、パネルには何も届かない。
#include "eye_actions.h"

#include "recenter.h"
#include "runtime.h"

#include <signal.h>
#include <sys/stat.h>

#include <cctype>
#include <chrono>

namespace {

/**
 * 版の文字列を数字の並びにする（"0.8.0-rc1" → 0, 8, 0。数字でない所で終わる）。
 * @param text 版
 * @return 数字の並び
 */
std::vector<long> versionParts(const std::string& text) {
    std::vector<long> parts;
    size_t i = 0;
    while (i < text.size() && std::isdigit(static_cast<unsigned char>(text[i]))) {
        long value = 0;
        while (i < text.size() && std::isdigit(static_cast<unsigned char>(text[i]))) {
            if (value < 100000000) value = value * 10 + (text[i] - '0');
            ++i;
        }
        parts.push_back(value);
        if (i >= text.size() || text[i] != '.') break;
        ++i;
    }
    return parts;
}

/**
 * 頼むことの名前（--action の引数・ログ用）。
 * @param call 頼むこと
 * @return "recenter" / "fit" / "stop"
 */
const char* callName(EyeCall call) {
    switch (call) {
        case EyeCall::Recenter: return "recenter";
        case EyeCall::Fit: return "fit";
        case EyeCall::Stop: return "stop";
        case EyeCall::Relearn: return "relearn";
    }
    return "";
}

/**
 * frameeyeosc-panel が標準エラーに書いた 1 行（ログ用。改行は除き、長すぎれば切る）。
 * @param err 標準エラー
 * @return 1 行（無ければ空）
 */
std::string firstLine(const std::string& err) {
    std::string line = err.substr(0, err.find('\n'));
    while (!line.empty() && (line.back() == '\r' || line.back() == ' ')) line.pop_back();
    if (line.size() > 200) line = line.substr(0, 200) + "…";
    return line;
}

/**
 * 失敗の結果を作る。
 * @param reason 理由のコード
 * @param detail ログ用の説明
 * @return 結果
 */
ActionOutcome eyeFailure(const std::string& reason, const std::string& detail) {
    ActionOutcome out;
    out.reason = reason;
    out.detail = detail;
    return out;
}

/**
 * 終了コードの読み方の表（0 は受け付けたので、ここには無い）。frameeyeosc 側でコードが増えたら、ここに 1 行足す。
 * フィットの途中（6）だけ、eye_fit なら止める（切り替え）。正面合わせの途中（10）はすぐ終わるので止めない。
 */
const EyeExitRule kEyeExitRules[] = {
    // 本物のアクションで 1 が返るのは、実行ファイルを入れ替えたあとも古いパネルが動いているときもある（パネルの再起動が要る）
    {eye_exit::kUsage, kReasonEyeOutdated},
    {eye_exit::kNoPanel, kReasonEyePanelStopped},
    {eye_exit::kNoAnswer, kReasonEyeNoAnswer},
    {eye_exit::kNotRunning, kReasonEyeNotRunning},
    {eye_exit::kNotFitted, kReasonEyeNotFitted},
    {eye_exit::kFitBusy, kReasonEyeBusy, true},
    {eye_exit::kNothingToStop, "failed", false, true},
    {eye_exit::kLocked, kReasonEyeLocked},
    {eye_exit::kNoEyeData, kReasonNotWorn},
    {eye_exit::kRecenterBusy, kReasonEyeRecenterBusy},
    {eye_exit::kNoCameras, kReasonEyeNoCameras},
};

/**
 * 目のカメラが使えない（11）のわけ。frameeyeosc-panel の標準エラー（"relearn: eye cameras not ready (calibrating)" など）に
 * この言葉があれば、その理由にする。無ければ表のまま（「目のカメラが使えません」）。
 */
const EyeErrHint kEyeCameraHints[] = {
    {eye_exit::kNoCameras, "calibrating", kReasonEyeCamerasCalibrating},
    {eye_exit::kNoCameras, "no eye video", kReasonEyeCamerasNoVideo},
};

}  // namespace

const EyeExitRule* findEyeExitRule(int code) {
    for (const EyeExitRule& rule : kEyeExitRules) {
        if (rule.code == code) return &rule;
    }
    return nullptr;
}

bool isEyeAction(ActionType type) {
    return type == ActionType::EyeRecenter || type == ActionType::EyeFit || type == ActionType::EyeRelearn;
}

bool parseEyePanelVersion(const std::string& output, std::string& version) {
    const std::string prefix = std::string(kEyePanelName) + " ";
    const size_t at = output.find(prefix);
    if (at == std::string::npos) return false;
    size_t begin = at + prefix.size();
    size_t end = begin;
    while (end < output.size() && !std::isspace(static_cast<unsigned char>(output[end]))) ++end;
    const std::string token = output.substr(begin, end - begin);
    if (versionParts(token).empty()) return false;
    version = token;
    return true;
}

bool eyePanelSupportsActions(const CommandResult& result) {
    return result.status == CommandResult::Status::Failed && result.exitCode == eye_exit::kUsage &&
           result.err.find(kEyeUnknownActionText) != std::string::npos;
}

EyePanelState eyePanelStateFor(bool found, bool supportsActions) {
    if (!found) return EyePanelState::Missing;
    return supportsActions ? EyePanelState::Ready : EyePanelState::Outdated;
}

std::vector<std::string> eyeActionArgv(const std::string& path, EyeCall call) {
    return {path, "--action", callName(call)};
}

std::vector<std::string> eyeProbeArgv(const std::string& path) {
    return {path, "--action", kEyeProbeAction};
}

ActionOutcome eyeOutcomeFor(EyeCall call, const CommandResult& result) {
    const std::string what = std::string(kEyePanelName) + " --action " + callName(call);
    const std::string said = firstLine(result.err);
    const std::string tail = said.empty() ? std::string() : "（" + said + "）";
    const EyeExitRule* rule = result.status == CommandResult::Status::Failed ? findEyeExitRule(result.exitCode) : nullptr;
    ActionOutcome out;
    if (result.status == CommandResult::Status::Timeout) {
        out = eyeFailure(kReasonEyeNoAnswer, what + ": " + std::to_string(kEyeActionTimeoutMs / 1000) + " 秒たっても終わりません");
    } else if (result.status == CommandResult::Status::SpawnFailed) {
        // exec できない（消えた・実行できない）: 入っていないのと同じに扱う
        out = eyeFailure(kReasonEyeMissing, what + " を起動できません: " + result.err);
    } else if (result.ok()) {
        out.ok = true;
        out.value = call == EyeCall::Recenter ? kEyeRecentering
                    : call == EyeCall::Fit    ? kEyeFitting
                    : call == EyeCall::Relearn ? kEyeRelearning
                                               : kEyeStopped;
        out.detail = what + ": 受け付けました";
    } else if (call == EyeCall::Stop && rule != nullptr && rule->okForStop) {
        // 途中だと言われてから止めるまでの間に終わっていた（止めたかったものはもう動いていない）
        out.ok = true;
        out.value = kEyeStopped;
        out.detail = what + ": 止めるものはもうありませんでした";
    } else {
        const char* reason = rule != nullptr ? rule->reason : "failed";  // 表に無いコードは「うまくいきませんでした」
        for (const EyeErrHint& hint : kEyeCameraHints) {
            if (hint.code == result.exitCode && result.err.find(hint.text) != std::string::npos) reason = hint.reason;
        }
        const std::string code = result.exitCode >= 0 ? "終了コード " + std::to_string(result.exitCode) : "シグナルで終了";
        out = eyeFailure(reason, what + ": " + code + tail);
    }
    if (call == EyeCall::Stop && !out.ok) out.value = kEyeStopped;  // 止めようとしたことを、お知らせの見出しに使う
    if (out.ok && !said.empty()) out.detail += tail;
    return out;
}

ActionOutcome runEyeAction(ActionType type, const EyePanelInfo& info, const EyeRunner& run) {
    if (!isEyeAction(type)) return eyeFailure("failed", "目のアクションではありません");
    if (info.state == EyePanelState::Missing || info.path.empty()) {
        return eyeFailure(kReasonEyeMissing, std::string(kEyePanelName) + " が入っていません");
    }
    if (info.state == EyePanelState::Outdated) {
        const std::string which = info.version.empty() ? std::string() : " " + info.version;
        return eyeFailure(kReasonEyeOutdated, std::string(kEyePanelName) + which + " は --action に対応していません");
    }
    const EyeCall call = type == ActionType::EyeRecenter ? EyeCall::Recenter
                         : type == ActionType::EyeFit     ? EyeCall::Fit
                                                          : EyeCall::Relearn;  // 覚え直しは切り替えにしない（止めるものが無い）
    const CommandResult first = run(eyeActionArgv(info.path, call), kEyeActionTimeoutMs);
    const EyeExitRule* rule = first.status == CommandResult::Status::Failed ? findEyeExitRule(first.exitCode) : nullptr;
    if (call == EyeCall::Fit && rule != nullptr && rule->stopsFit) {
        // フィットの途中にもう一度押した: 止める（うっかりの長押しの逃げ道。フィット自体はもう一度頼んでも切り替わらない）
        const CommandResult stop = run(eyeActionArgv(info.path, EyeCall::Stop), kEyeActionTimeoutMs);
        ActionOutcome out = eyeOutcomeFor(EyeCall::Stop, stop);
        out.detail = "フィットの途中なので止めます → " + out.detail;
        return out;
    }
    return eyeOutcomeFor(call, first);
}

ActionOutcome runEyeAction(ActionType type) {
    static EyePanelProbe probe;  // 作業用スレッド（--test-action ではメインスレッド）だけで使う
    probe.refresh();
    return runEyeAction(type, probe.info(),
                        [](const std::vector<std::string>& argv, int timeoutMs) { return runCommand(argv, timeoutMs); });
}

EyePanelProbe::EyePanelProbe(EyeRunner run, EyeClock now) : run_(std::move(run)), now_(std::move(now)) {
    if (!run_) run_ = [](const std::vector<std::string>& argv, int timeoutMs) { return runCommand(argv, timeoutMs); };
    if (!now_) {
        now_ = [] {
            using namespace std::chrono;
            return static_cast<long long>(duration_cast<milliseconds>(steady_clock::now().time_since_epoch()).count());
        };
    }
}

bool EyePanelProbe::refresh() {
    EyePanelInfo next;
    next.path = findExecutable(kEyePanelName);
    struct stat st {};
    if (next.path.empty() || ::stat(next.path.c_str(), &st) != 0) {
        next.path.clear();
        next.state = EyePanelState::Missing;
        key_.clear();
        retryAtMs_ = 0;
    } else {
        const std::string key = next.path + "\n" + std::to_string(static_cast<long long>(st.st_mtim.tv_sec)) + "." +
                                std::to_string(st.st_mtim.tv_nsec) + "\n" + std::to_string(static_cast<long long>(st.st_size));
        if (key == key_ && info_.state != EyePanelState::Unknown) return false;
        if (retryAtMs_ != 0 && now_() < retryAtMs_) return false;  // 前の確かめが答えなかった: 少し待ってからやり直す
        // 対応しているかは --action bogus の答え（1 と "unknown action"）で決める。版は「今は 0.7.9」と見せるためだけ
        const CommandResult probe = run_(eyeProbeArgv(next.path), kEyeProbeTimeoutMs);
        if (probe.status == CommandResult::Status::Timeout || probe.status == CommandResult::Status::SpawnFailed) {
            // 答えが出なかったので覚えない（「更新が必要」のまま固定しない）。起動できないなら入っていない側にする
            retryAtMs_ = now_() + kEyeProbeRetryMs;
            if (probe.status == CommandResult::Status::SpawnFailed) {
                next.path.clear();
                next.state = EyePanelState::Missing;
            }
        } else {
            key_ = key;
            retryAtMs_ = 0;
            next.state = eyePanelStateFor(true, eyePanelSupportsActions(probe));
            const CommandResult version = run_({next.path, "--version"}, kEyeProbeTimeoutMs);
            if (!version.ok() || !parseEyePanelVersion(version.out, next.version)) next.version.clear();
        }
    }
    if (next == info_) return false;
    info_ = next;
    return true;
}

EyePanelWatcher::~EyePanelWatcher() {
    stop();
}

void EyePanelWatcher::start() {
    if (thread_.joinable()) return;
    stopping_ = false;
    // 新しいスレッドはシグナルのマスクを引き継ぐので、作る間だけすべて止める（SIGTERM・SIGUSR1 はメインスレッドで受ける）
    sigset_t all;
    sigset_t old;
    sigfillset(&all);
    ::pthread_sigmask(SIG_SETMASK, &all, &old);
    thread_ = std::thread([this] { run(); });
    ::pthread_sigmask(SIG_SETMASK, &old, nullptr);
}

void EyePanelWatcher::request() {
    {
        std::lock_guard<std::mutex> lock(mutex_);
        requested_ = true;
    }
    wake_.notify_one();
}

bool EyePanelWatcher::take(EyePanelInfo& info) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!changed_) return false;
    changed_ = false;
    info = latest_;
    return true;
}

void EyePanelWatcher::stop() {
    {
        std::lock_guard<std::mutex> lock(mutex_);
        stopping_ = true;
        requested_ = false;
    }
    wake_.notify_one();
    if (thread_.joinable()) thread_.join();
}

void EyePanelWatcher::run() {
    while (true) {
        {
            std::unique_lock<std::mutex> lock(mutex_);
            wake_.wait(lock, [this] { return stopping_ || requested_; });
            if (stopping_) return;
            requested_ = false;
        }
        if (!probe_.refresh()) continue;
        std::lock_guard<std::mutex> lock(mutex_);
        latest_ = probe_.info();
        changed_ = true;
    }
}
