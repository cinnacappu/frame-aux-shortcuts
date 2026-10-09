// 作業用スレッドの実装。
#include "action_worker.h"

#include <unistd.h>

#include <cstdio>

/** 積んでおける処理の上限（連打で wpctl が詰まっても、際限なく溜めない）。 */
constexpr size_t kMaxQueuedJobs = 4;

ActionWorker::~ActionWorker() {
    stop();
}

void ActionWorker::start(int wakeFd) {
    if (thread_.joinable()) return;
    stopping_ = false;
    wakeFd_ = wakeFd;
    thread_ = std::thread([this] { run(); });
}

void ActionWorker::post(const std::string& label, const PressRecord& record, DeferredAction action) {
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (jobs_.size() >= kMaxQueuedJobs) {
            std::fprintf(stderr, "[アクション] %s: 前の処理が詰まっているので捨てました\n", label.c_str());
            return;
        }
        jobs_.push_back(Job {label, record, std::move(action)});
    }
    wake_.notify_one();
}

std::vector<FinishedAction> ActionWorker::takeFinished() {
    std::lock_guard<std::mutex> lock(mutex_);
    std::vector<FinishedAction> out;
    out.swap(finished_);
    return out;
}

void ActionWorker::stop() {
    {
        std::lock_guard<std::mutex> lock(mutex_);
        stopping_ = true;
        jobs_.clear();
    }
    wake_.notify_one();
    if (thread_.joinable()) thread_.join();
}

void ActionWorker::run() {
    while (true) {
        Job job;
        {
            std::unique_lock<std::mutex> lock(mutex_);
            wake_.wait(lock, [this] { return stopping_ || !jobs_.empty(); });
            if (stopping_) return;
            job = std::move(jobs_.front());
            jobs_.pop_front();
        }
        const ActionOutcome outcome = job.action();
        {
            std::lock_guard<std::mutex> lock(mutex_);
            finished_.push_back(FinishedAction {job.label, job.record, outcome});
        }
        if (wakeFd_ >= 0) {
            const char byte = 1;
            if (::write(wakeFd_, &byte, 1) < 0) {
                // パイプがいっぱい（メインスレッドがまだ読んでいない）なら、起こす知らせは届いている
            }
        }
    }
}
