// カーソルの判定と、aux の扱いの決め方の実装。
#include "head_pointer.h"

#include <algorithm>

bool isHeadPointer(const PointerSample& sample) {
    return sample.controllers <= 0;
}

AuxMode decideAuxMode(bool shortcutsOn, bool handBackOption, bool headPointer) {
    if (!shortcutsOn) return AuxMode::Original;
    return handBackOption && headPointer ? AuxMode::HandedBack : AuxMode::Shortcuts;
}

void HeadPointerTracker::reset(int64_t nowMs) {
    stable_ = false;
    waiting_ = false;
    since_ = nowMs;
    graceUntil_ = nowMs + params_.startGraceMs;
}

bool HeadPointerTracker::update(bool headPointer, int64_t nowMs) {
    if (headPointer == stable_) {
        waiting_ = false;
        return stable_;
    }
    if (!waiting_) {
        waiting_ = true;
        since_ = nowMs;
    }
    int64_t at = since_ + (headPointer ? params_.enterMs : params_.leaveMs);
    if (headPointer) at = std::max(at, graceUntil_);
    if (nowMs >= at) {
        stable_ = headPointer;
        waiting_ = false;
    }
    return stable_;
}
