// マイクのミュートの見張り役の実装。
#include "mic_monitor.h"

#include "command.h"
#include "runtime.h"

#include <unistd.h>

#include <chrono>
#include <cstdio>

MicMonitor::~MicMonitor() {
    stop();
}

void MicMonitor::start(int wakeFd) {
    if (thread_.joinable()) return;
    wakeFd_ = wakeFd;
    stopping_ = false;
    thread_ = std::thread([this] { run(); });
}

void MicMonitor::stop() {
    {
        std::lock_guard<std::mutex> lock(mutex_);
        stopping_ = true;
    }
    wake_.notify_one();
    if (thread_.joinable()) thread_.join();
}

void MicMonitor::setIntervalMs(int ms) {
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (ms == intervalMs_) return;
        intervalMs_ = ms;
        kick_ = true;
    }
    wake_.notify_one();
}

void MicMonitor::run() {
    bool loggedFailure = false;
    while (true) {
        const CommandResult result = runCommand({"wpctl", "get-volume", "@DEFAULT_AUDIO_SOURCE@"}, 2000);
        bool muted = false;
        int value = -1;
        if (result.ok() && parseWpctlMuted(result.out, muted)) {
            value = muted ? 1 : 0;
            loggedFailure = false;
        } else if (!loggedFailure) {
            std::fprintf(stderr, "[マイク] ミュートの状態を読めません: %s\n",
                         describeCommand({"wpctl", "get-volume", "@DEFAULT_AUDIO_SOURCE@"}, result).c_str());
            loggedFailure = true;
        }
        if (value != muted_.load()) {
            muted_.store(value);
            generation_.fetch_add(1);
            if (wakeFd_ >= 0) {
                const char byte = 1;
                if (::write(wakeFd_, &byte, 1) < 0) {
                    // パイプがいっぱいなら、起こす知らせは届いている
                }
            }
        }
        std::unique_lock<std::mutex> lock(mutex_);
        wake_.wait_for(lock, std::chrono::milliseconds(intervalMs_), [this] { return stopping_ || kick_; });
        if (stopping_) return;
        kick_ = false;
    }
}
