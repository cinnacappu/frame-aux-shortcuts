// aux ボタンの押し方（長押し・1〜5 回押し）の判定。時刻を引数で受け取る純粋なクラスなので、実機無しで試せる。
#pragma once

#include <cstdint>

/** 判定の結果。 */
struct PressResult {
    enum class Kind {
        None,       ///< まだ確定していない（何もしない）
        LongPress,  ///< 長押し
        Count,      ///< 1〜maxCount 回押し（count に回数）
        TooMany,    ///< maxCount を超えて押した（何もしない。ログ用に count に回数）
    };
    Kind kind = Kind::None;
    int count = 0;

    /** @return 何か確定していれば true */
    bool decided() const { return kind != Kind::None; }
};

/**
 * 押し方の判定。
 * - 長押し: 押してから longPressMs たっても離されなければ、その時点で確定。確定後の離しは数えない。
 *   それまでに数えていた回数（押す・離すの途中で長押しになった場合）は捨てる
 * - 回数: 最後に離してから multiPressGapMs たったら確定。maxCount を超えたら TooMany（何もしない）
 *
 * 呼び方: キーの押し・離しは onKey()、それ以外のときは nextDeadlineMs() の時刻に onTick() を呼ぶ。
 * どちらも、時刻が過ぎていた締め切りを先に片付けてから処理する。1 回の呼び出しで確定するのは多くても 1 つ。
 */
class PressDetector {
public:
    /** 判定に使う時間と回数。 */
    struct Params {
        int64_t longPressMs = 600;      ///< 長押しとみなすまでの時間（ms）
        int64_t multiPressGapMs = 350;  ///< 最後に離してから回数を確定するまでの時間（ms）
        int maxCount = 5;               ///< これを超えたら無視する
    };

    /** 既定の時間と回数で作る。 */
    PressDetector() = default;

    /** @param params 判定に使う時間と回数 */
    explicit PressDetector(const Params& params) : params_(params) {}

    /**
     * 判定の時間を変える（設定の読み直し用）。途中の状態はそのまま。
     * @param params 新しい時間と回数
     */
    void setParams(const Params& params) { params_ = params; }

    /** @return 今の判定の時間と回数 */
    const Params& params() const { return params_; }

    /**
     * キーが押された・離された。
     * @param down 押されたら true、離されたら false
     * @param nowMs その時刻（ms、単調増加）
     * @return 確定した押し方（無ければ None）
     */
    PressResult onKey(bool down, int64_t nowMs);

    /**
     * 時間の経過だけを伝える（締め切りが来ていれば確定する）。
     * @param nowMs 今の時刻（ms、単調増加）
     * @return 確定した押し方（無ければ None）
     */
    PressResult onTick(int64_t nowMs);

    /**
     * 次に onTick() を呼ぶべき時刻。
     * @return 時刻（ms）。待っている締め切りが無ければ -1
     */
    int64_t nextDeadlineMs() const;

    /** 途中の状態を捨てる（イベントの取りこぼし SYN_DROPPED のとき）。 */
    void reset();

    /** @return キーが押されたままと見ていれば true */
    bool pressed() const { return pressed_; }

private:
    Params params_;
    bool pressed_ = false;       ///< 押されている
    bool longFired_ = false;     ///< この押しで長押しが確定した（離すまで何もしない）
    int count_ = 0;              ///< 今の一連で離した回数
    int64_t downAtMs_ = 0;       ///< 最後に押した時刻
    int64_t upAtMs_ = 0;         ///< 最後に離した時刻
};
