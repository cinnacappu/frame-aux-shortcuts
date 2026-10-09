// 外部コマンドの終わりを待つアクションを、イベントループの外で順番に動かす作業用スレッド。
// 終わった結果はメインスレッドが takeFinished() で受け取る（音・視界の表示・状態ファイルはメインスレッドで扱う）。
#pragma once

#include "actions.h"
#include "status.h"

#include <condition_variable>
#include <deque>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

/** 終わった処理 1 つ。 */
struct FinishedAction {
    std::string label;     ///< ログ用の名前
    PressRecord record;    ///< 押し方（結果はまだ入っていない）
    ActionOutcome outcome; ///< 結果
};

/**
 * 作業用スレッド。post() で積んだ処理を 1 つずつ動かし、終わったら結果を溜めて wakeFd に 1 バイト書く。
 * スレッドを作る前に、SIGTERM などはメインスレッドで止めて（マスクして）おくこと（このスレッドはマスクを引き継ぐ）。
 */
class ActionWorker {
public:
    ActionWorker() = default;
    ActionWorker(const ActionWorker&) = delete;
    ActionWorker& operator=(const ActionWorker&) = delete;
    ~ActionWorker();

    /**
     * スレッドを始める。
     * @param wakeFd 終わったときに 1 バイト書くパイプ（メインスレッドの待ちを起こす。-1 なら書かない）
     */
    void start(int wakeFd);

    /**
     * 処理を積む。
     * @param label ログ用の名前（"1 回押し → mic_mute_toggle" など）
     * @param record 押し方
     * @param action 動かす処理
     */
    void post(const std::string& label, const PressRecord& record, DeferredAction action);

    /**
     * 終わった処理を受け取る（溜まっていた分をすべて）。
     * @return 終わった順
     */
    std::vector<FinishedAction> takeFinished();

    /** 積んである処理のうち、まだ始めていないものは捨てて、動いている処理が終わるのを待ってから止める。 */
    void stop();

private:
    /** 積んだ処理 1 つ。 */
    struct Job {
        std::string label;
        PressRecord record;
        DeferredAction action;
    };

    /** スレッドの本体。 */
    void run();

    std::thread thread_;
    std::mutex mutex_;
    std::condition_variable wake_;
    std::deque<Job> jobs_;
    std::vector<FinishedAction> finished_;
    bool stopping_ = false;
    int wakeFd_ = -1;
};
