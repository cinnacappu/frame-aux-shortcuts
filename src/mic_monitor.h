// マイクのミュートを数秒おきに読む見張り役（aux 以外でミュートされたときも追う）。
// 読むだけ（wpctl get-volume）。PipeWire の設定は変えない。
#pragma once

#include <atomic>
#include <condition_variable>
#include <mutex>
#include <thread>

/**
 * 別スレッドで `wpctl get-volume @DEFAULT_AUDIO_SOURCE@` を間隔ごとに動かし、ミュートが変わったら wakeFd に知らせる。
 * スレッドを作る前に、SIGTERM などはメインスレッドで止めて（マスクして）おくこと。
 */
class MicMonitor {
public:
    MicMonitor() = default;
    MicMonitor(const MicMonitor&) = delete;
    MicMonitor& operator=(const MicMonitor&) = delete;
    ~MicMonitor();

    /**
     * 始める。
     * @param wakeFd 変わったときに 1 バイト書くパイプ（-1 なら書かない）
     */
    void start(int wakeFd);

    /** 止める（動いている wpctl が終わるのを待つ。最長 2 秒）。 */
    void stop();

    /**
     * 読む間隔を変える（すぐ次を読む）。
     * @param ms 間隔（ms）
     */
    void setIntervalMs(int ms);

    /** @return 1 = ミュート、0 = ミュートでない、-1 = 分からない */
    int muted() const { return muted_.load(); }

    /** @return 値が変わるたびに増える番号 */
    unsigned generation() const { return generation_.load(); }

private:
    /** スレッドの本体。 */
    void run();

    std::thread thread_;
    std::mutex mutex_;
    std::condition_variable wake_;
    bool stopping_ = false;
    bool kick_ = false;
    int intervalMs_ = 1000;
    int wakeFd_ = -1;
    std::atomic<int> muted_ {-1};
    std::atomic<unsigned> generation_ {0};
};
