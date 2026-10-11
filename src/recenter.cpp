// 視点のリセットの判断の実装。
#include "recenter.h"

RecenterDecision RecenterGate::decide(const RecenterState& state, int64_t nowMs) const {
    if (!state.connected) return RecenterDecision::NoSteamVr;
    // 外して置いている間は姿勢が古いか取れないので、その向きを正面にしない
    if (!state.hmdInUse) return RecenterDecision::NotWorn;
    if (!state.hmdTracking) return RecenterDecision::NotTracking;
    if (resetOnce_ && nowMs - lastResetMs_ < kRecenterRepeatMs) return RecenterDecision::Repeated;
    return RecenterDecision::Reset;
}

const char* recenterFailureReason(RecenterDecision decision) {
    switch (decision) {
        case RecenterDecision::NoSteamVr: return "no_steamvr";
        case RecenterDecision::NotWorn: return kReasonNotWorn;
        case RecenterDecision::NotTracking: return kReasonNotTracking;
        case RecenterDecision::Reset:
        case RecenterDecision::Repeated: return "";
    }
    return "";
}
