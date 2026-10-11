// 2 回・3 回タップの判定の実装。
#include "tap_detector.h"

#include <cmath>

void TapDetector::reset() {
    restart();
    count_ = 0;
    lockoutUntilUs_ = 0;
}

void TapDetector::restart() {
    havePrev_ = false;
    speeds_.clear();
    holdUntilUs_ = 0;
}

TapResult TapDetector::finishGroup() {
    TapResult result;
    result.taps = count_;
    count_ = 0;
    // 無視する時間は、確定した時刻ではなく組の最後のタップから数える（3 回目を待って確定したときも、次の組を早く受けられる）
    if (result.fired()) lockoutUntilUs_ = lastTapUs_ + kTapLockoutUs;
    return result;
}

TapResult TapDetector::onSample(int64_t tUs, double wx, double wy, double wz) {
    TapResult result;
    if (havePrev_) {
        const int64_t dt = tUs - prevUs_;
        if (dt <= 0) return result;  // 同じ時刻（か巻き戻り）の読み取りは使わない
        // 間が空いた（読めなかった・メインループが止まっていた）ら、角加速度はつながらないので読み取りだけ最初から。
        // 途中の組は残す（組の区切りは最後のタップの時刻だけで決まるので、2 回目のあと止まっても 2 回で確定できる）
        if (dt > kTapMaxSampleGapUs) restart();
    }
    // 山が来ないまま組の区切りを過ぎたら確定する
    if (count_ > 0 && tUs - lastTapUs_ > kTapMaxGapUs) result = finishGroup();

    const double w[3] = {wx, wy, wz};
    const double magnitude = std::sqrt(wx * wx + wy * wy + wz * wz);
    double accel = 0.0;
    const bool haveAccel = havePrev_;
    if (havePrev_) {
        const double dx = wx - prev_[0];
        const double dy = wy - prev_[1];
        const double dz = wz - prev_[2];
        accel = std::sqrt(dx * dx + dy * dy + dz * dz) / (static_cast<double>(tUs - prevUs_) / 1e6);
    }
    havePrev_ = true;
    prevUs_ = tUs;
    for (int k = 0; k < 3; ++k) prev_[k] = w[k];

    // 頭が止まっていたか（この読み取りを足す前の記録で、山の kTapGateFromUs 前〜kTapGateToUs 前の平均）
    double sum = 0.0;
    int samples = 0;
    for (const Speed& s : speeds_) {
        const int64_t before = tUs - s.tUs;
        if (before < kTapGateFromUs && before >= kTapGateToUs) {
            sum += s.magnitude;
            ++samples;
        }
    }
    speeds_.push_back({tUs, magnitude});
    while (!speeds_.empty() && tUs - speeds_.front().tUs >= kTapGateFromUs) speeds_.pop_front();

    if (!haveAccel || tUs < holdUntilUs_ || accel <= kTapPeakAngAccel) return result;
    holdUntilUs_ = tUs + kTapPeakHoldUs;
    if (tUs < lockoutUntilUs_) return result;
    // 区間に読み取りが無い（読み始めた直後）ときは、止まっていたか分からないので数えない
    if (samples == 0 || sum / samples >= kTapGateAngVel) return result;

    if (count_ > 0 && tUs - lastTapUs_ < kTapMinGapUs) {
        // 前のタップに近すぎる: 前の組はそこまでで確定し、このタップから新しい組にする
        result = finishGroup();
        if (tUs < lockoutUntilUs_) return result;
    }
    ++count_;
    lastTapUs_ = tUs;
    if (fireDoubleImmediately_ && count_ == 2) result = finishGroup();
    return result;
}
