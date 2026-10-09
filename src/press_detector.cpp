// 押し方の判定の実装。
#include "press_detector.h"

PressResult PressDetector::onTick(int64_t nowMs) {
    PressResult result;
    if (pressed_) {
        if (!longFired_ && nowMs - downAtMs_ >= params_.longPressMs) {
            longFired_ = true;
            count_ = 0;  // 途中まで数えていた回数は捨てる
            result.kind = PressResult::Kind::LongPress;
        }
        return result;
    }
    if (count_ > 0 && nowMs - upAtMs_ >= params_.multiPressGapMs) {
        result.kind = count_ > params_.maxCount ? PressResult::Kind::TooMany : PressResult::Kind::Count;
        result.count = count_;
        count_ = 0;
    }
    return result;
}

PressResult PressDetector::onKey(bool down, int64_t nowMs) {
    // 先に、この時刻までに来ていた締め切りを片付ける（イベントがまとめて届いたときも順番どおりに判定する）
    PressResult result = onTick(nowMs);
    if (down) {
        if (pressed_) return result;  // 押したままの重複は無視
        pressed_ = true;
        longFired_ = false;
        downAtMs_ = nowMs;
        return result;
    }
    if (!pressed_) return result;  // 押した記録の無い離しは無視
    pressed_ = false;
    if (longFired_) {
        longFired_ = false;  // 長押しの後の離しは数えない
        return result;
    }
    ++count_;
    upAtMs_ = nowMs;
    return result;
}

int64_t PressDetector::nextDeadlineMs() const {
    if (pressed_) return longFired_ ? -1 : downAtMs_ + params_.longPressMs;
    if (count_ > 0) return upAtMs_ + params_.multiPressGapMs;
    return -1;
}

void PressDetector::reset() {
    pressed_ = false;
    longFired_ = false;
    count_ = 0;
}
