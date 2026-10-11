// SteamVR がヘッドセットのカーソル（画面の真ん中の点。頭の向きで動かし、aux でクリックする）で操作しているかの判定と、
// aux を置き換えるか・SteamVR に返すかの決め方（設定 original_aux_on_head_pointer）。
// OpenVR は使わず、読んだ値と時刻を引数で受け取るだけなので、実機無しで試せる。値を読むのは vr_client.h。
#pragma once

#include <cstdint>

/** 機器が無いときの番号（vr::k_unTrackedDeviceIndexInvalid と同じ）。 */
constexpr uint32_t kNoTrackedDevice = 0xFFFFFFFFu;

/** SteamVR から読んだ、カーソルの判定に使う値。 */
struct PointerSample {
    int controllers = 0;                           ///< つながっているコントローラーの数（判定に使う）
    bool dashboardVisible = false;                 ///< ダッシュボードが開いている（ログ用）
    uint32_t dashboardPointer = kNoTrackedDevice;  ///< ダッシュボードのポインターの機器（GetPrimaryDashboardDevice。ログ用）
};

/**
 * SteamVR がヘッドセットのカーソルで操作している（はずの）状態か。コントローラーが 1 本もつながっていなければカーソル。
 * ダッシュボードのポインターの機器（GetPrimaryDashboardDevice）は合図にしない: 実機（2026-10-11）では、ポインターだった
 * コントローラーの電源を切っても、その番号のまま残っていた（変わるのが遅い）。コントローラーの数はすぐ変わる。
 * @param sample 読んだ値
 * @return カーソルなら true
 */
bool isHeadPointer(const PointerSample& sample);

/** aux をどう扱うか。 */
enum class AuxMode {
    Original,    ///< 置き換えない（パネルを閉じたら戻す設定で、パネルが動いていない）
    Shortcuts,   ///< 置き換えて、押し方をショートカットに使う
    HandedBack,  ///< カーソルの間なので、置き換えをやめて SteamVR に返している
};

/**
 * aux をどう扱うかを決める。
 * @param shortcutsOn ショートカットを使う（run_in_background か、設定パネルが動いている）
 * @param handBackOption 設定 original_aux_on_head_pointer
 * @param headPointer カーソルの状態（HeadPointerTracker で落ち着いたもの）
 * @return 扱い
 */
AuxMode decideAuxMode(bool shortcutsOn, bool handBackOption, bool headPointer);

/** HeadPointerTracker の待つ時間（ms）。 */
struct HeadPointerParams {
    int64_t enterMs = 2000;       ///< カーソルになってから切り替えるまで
    int64_t leaveMs = 500;        ///< カーソルでなくなってから切り替えるまで（ショートカットは早く戻す）
    int64_t startGraceMs = 5000;  ///< reset() から、カーソルに切り替えない時間（コントローラーがつながるのを待つ）
};

/**
 * カーソルの判定を落ち着かせる役。読んだ値が変わっても、しばらく同じ値が続いてから切り替える
 * （コントローラーが一瞬切れたときや、SteamVR の起動でコントローラーがつながるまでの間に、行ったり来たりしない）。
 */
class HeadPointerTracker {
public:
    /** @param params 待つ時間 */
    explicit HeadPointerTracker(HeadPointerParams params = HeadPointerParams()) : params_(params) {}

    /**
     * 最初の状態（カーソルでない）に戻す。SteamVR につないだときに呼ぶ。
     * @param nowMs 今の時刻（ms、単調増加）
     */
    void reset(int64_t nowMs);

    /**
     * 読んだ値を渡す。
     * @param headPointer isHeadPointer() の結果
     * @param nowMs 今の時刻（ms、単調増加）
     * @return 落ち着いた状態（カーソルなら true）
     */
    bool update(bool headPointer, int64_t nowMs);

    /** @return 落ち着いた状態（カーソルなら true） */
    bool headPointer() const { return stable_; }

private:
    HeadPointerParams params_;
    bool stable_ = false;      ///< 落ち着いた状態
    bool waiting_ = false;     ///< 違う値が続くのを待っている
    int64_t since_ = 0;        ///< 違う値になった時刻
    int64_t graceUntil_ = 0;   ///< これより前はカーソルに切り替えない
};
