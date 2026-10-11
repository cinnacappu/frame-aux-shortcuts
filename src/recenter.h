// 視点のリセット（今向いている方を正面にする）のうち、OpenVR を使わない部分: 実行してよいかの判断と、
// 続けて頼まれたときの間引き。OpenVR の呼び出し（IVRChaperone::ResetZeroPose）は本体の vr_client.h が行う。
// 単体テストできる。
#pragma once

#include <cstdint>

/** 視点のリセットの結果の値（ActionOutcome::value・PressRecord::value）。 */
constexpr const char* kRecentered = "recentered";       ///< リセットした
constexpr const char* kRecenterRepeated = "repeated";   ///< 直前にリセットしたばかりなので、し直さなかった

/** 失敗の理由のコード（ActionOutcome::reason）。 */
constexpr const char* kReasonNotWorn = "not_worn";          ///< ヘッドセットをかぶっていない（スタンバイなど）
constexpr const char* kReasonNotTracking = "not_tracking";  ///< ヘッドセットの姿勢が取れていない

/** 前のリセットからこれより短い間に来た 2 回目は、リセットし直さない（ms）。 */
constexpr int64_t kRecenterRepeatMs = 1000;

/** リセットしてよいかを決めるための、SteamVR から読んだ様子。 */
struct RecenterState {
    bool connected = false;  ///< SteamVR につながっている
    bool hmdInUse = false;   ///< ヘッドセットをかぶって使っている（GetTrackedDeviceActivityLevel が UserInteraction）
    bool hmdTracking = false;  ///< ヘッドセットの姿勢が正しく取れている（bPoseIsValid かつ TrackingResult_Running_OK）
};

/** リセットするかの判断。 */
enum class RecenterDecision {
    Reset,        ///< リセットする
    Repeated,     ///< 前のリセットから kRecenterRepeatMs たっていないので、し直さない（成功として扱う）
    NoSteamVr,    ///< SteamVR につながっていない
    NotWorn,      ///< ヘッドセットをかぶっていない
    NotTracking,  ///< ヘッドセットの姿勢が取れていない
};

/**
 * 視点のリセットの関所。最後にリセットした時刻を覚えて、続けて頼まれたときに間引く。
 * 失敗したもの・し直さなかったものは時刻に数えない（失敗のあとはすぐやり直せる）。
 */
class RecenterGate {
public:
    /**
     * リセットするかを決める。調べる順は、SteamVR → かぶっているか → 姿勢 → 前のリセットからの時間。
     * @param state SteamVR から読んだ様子
     * @param nowMs 今の時刻（ms、単調増加）
     * @return 判断
     */
    RecenterDecision decide(const RecenterState& state, int64_t nowMs) const;

    /**
     * リセットしたことを覚える。
     * @param nowMs リセットした時刻（ms、単調増加）
     */
    void markReset(int64_t nowMs) {
        resetOnce_ = true;
        lastResetMs_ = nowMs;
    }

private:
    bool resetOnce_ = false;  ///< 一度でもリセットしたか
    int64_t lastResetMs_ = 0; ///< 最後にリセットした時刻
};

/**
 * 判断に合う失敗の理由のコード。
 * @param decision 判断
 * @return "no_steamvr" / kReasonNotWorn / kReasonNotTracking。失敗でなければ空文字列
 */
const char* recenterFailureReason(RecenterDecision decision);
