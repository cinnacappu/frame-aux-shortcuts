// ヘッドセットを指で軽くたたく（2 回・3 回タップ）の判定。OpenVR は使わず、読んだ時刻と角速度を引数で受け取るだけなので、
// 実機無しで試せる。ヘッドセットの姿勢を読むのは vr_client.h、読む間隔を決めるのは main.cpp。
//
// 合図: たたくと、ヘッドセットの角速度（OpenVR の姿勢の vAngularVelocity）が一瞬だけ跳ねる（角加速度の山）。
// 頭を回したりうなずいたりしても角加速度は大きくなるので、山の直前に頭がほとんど止まっていたものだけをタップとみなす。
// 値は実機で録ったデータ（かぶって静止 20 秒・2 回タップ 13 組・3 回タップ 12 組・首振り・うなずき・歩く 60 秒）を
// 250 Hz に間引いて決めた（静止・動きで 0 件、2 回・3 回タップはすべて当たる）。
#pragma once

#include <cstdint>
#include <deque>

/** 姿勢を読む間隔（µs。250 Hz）。 */
constexpr int64_t kTapSamplePeriodUs = 4000;
/** 角加速度（rad/s²）がこれを超えたらタップの山。 */
constexpr double kTapPeakAngAccel = 60.0;
/** 山の始まりから、次の山を探さない時間（µs。1 回のタップで山は 1 つ）。 */
constexpr int64_t kTapPeakHoldUs = 80000;
/** 頭が止まっていたかを見る区間の始まり（山の何 µs 前から）。 */
constexpr int64_t kTapGateFromUs = 160000;
/** 頭が止まっていたかを見る区間の終わり（山の何 µs 前まで。たたいた揺れそのものは入れない）。 */
constexpr int64_t kTapGateToUs = 10000;
/** 区間の |角速度| の平均がこれ以上なら、頭を動かしたとみなして山を捨てる（rad/s）。 */
constexpr double kTapGateAngVel = 0.5;
/** 前の山からこの時間以上（µs）… */
constexpr int64_t kTapMinGapUs = 100000;
/** …この時間以内なら同じ組（2 回目・3 回目）。山が来ないままこの時間たったら、組の回数で確定する。 */
constexpr int64_t kTapMaxGapUs = 450000;
/** 2 回・3 回タップが確定したら、その組の最後のタップからこの時間はタップを無視する（µs）。 */
constexpr int64_t kTapLockoutUs = 1000000;
/** 読んだ間隔がこれより空いたら、途中の判定を捨てて最初から（µs）。 */
constexpr int64_t kTapMaxSampleGapUs = 20000;

/** 判定の結果。 */
struct TapResult {
    int taps = 0;  ///< 確定した組のタップの回数（0 なら何も確定していない。1 や 4 以上も入る）

    /** @return 2 回か 3 回タップが確定したら true（アクションを動かす） */
    bool fired() const { return taps == 2 || taps == 3; }
};

/**
 * 2 回・3 回タップの判定。
 * - 山: 前の読み取りからの角加速度 |ω_i − ω_{i−1}| / dt が kTapPeakAngAccel を超えた。そこから kTapPeakHoldUs は次の山を探さない
 * - 頭が止まっていたか: 山の kTapGateFromUs 前から kTapGateToUs 前までの |ω| の平均が kTapGateAngVel 未満なら、タップとして数える
 * - 組: 前のタップから kTapMinGapUs〜kTapMaxGapUs の間に来たタップは同じ組。来ないまま kTapMaxGapUs たったら組の回数で確定
 * - 3 回タップを使わないとき（setFireDoubleImmediately(true)）は、2 回目のタップでその場で確定する（待たない）
 * - 2 回・3 回タップが確定したら、その組の最後のタップから kTapLockoutUs はタップを無視する
 *
 * 呼び方: 姿勢を読めるたびに onSample() を呼ぶ（読まない間は呼ばない。読むのをやめるときは reset()）。
 */
class TapDetector {
public:
    /**
     * 3 回タップを待たずに、2 回目のタップでその場で 2 回タップにするか（3 回タップに何も割り当てていないとき）。
     * @param on そうするなら true
     */
    void setFireDoubleImmediately(bool on) { fireDoubleImmediately_ = on; }

    /**
     * 読んだ角速度を渡す。
     * @param tUs 読んだ時刻（µs、単調増加）
     * @param wx 角速度の x（rad/s）
     * @param wy 角速度の y（rad/s）
     * @param wz 角速度の z（rad/s）
     * @return 確定した組（無ければ taps = 0）
     */
    TapResult onSample(int64_t tUs, double wx, double wy, double wz);

    /** 途中の状態を捨てる（読むのをやめるとき。タップを無視する時間も忘れる）。 */
    void reset();

private:
    /** 角速度の大きさの記録（頭が止まっていたかを見るため）。 */
    struct Speed {
        int64_t tUs;
        double magnitude;
    };

    /** 読み取りの続きを捨てる（前の値・記録）。途中の組とタップを無視する時間はそのまま。 */
    void restart();

    /**
     * 途中の組を確定する。
     * @return 確定した組
     */
    TapResult finishGroup();

    bool fireDoubleImmediately_ = false;
    bool havePrev_ = false;       ///< 前の読み取りがある
    int64_t prevUs_ = 0;          ///< 前の読み取りの時刻
    double prev_[3] = {0, 0, 0};  ///< 前の読み取りの角速度
    std::deque<Speed> speeds_;    ///< 直近 kTapGateFromUs の |ω|
    int64_t holdUntilUs_ = 0;     ///< これより前は次の山を探さない
    int64_t lockoutUntilUs_ = 0;  ///< これより前のタップは無視する
    int count_ = 0;               ///< 今の組のタップの回数
    int64_t lastTapUs_ = 0;       ///< 今の組の最後のタップの時刻
};
