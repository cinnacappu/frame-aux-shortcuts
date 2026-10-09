// 視界の表示のオーバーレイ（頭に付いてくる 2 枚: 下寄りに押したときのお知らせ、上寄りにミュート中の表示）。
// Vulkan とオーバーレイは、最初に何かを出すときに作る（出さなければ GPU を使わない）。
// 終わるときの順番は frame-perf-overlay と同じ: releaseOverlays() → VR_Shutdown → releaseVulkan()。
#pragma once

#include "vk_texture.h"

#include <cstdint>
#include <limits>
#include <string>
#include <utility>
#include <vector>

/**
 * 視界の表示のオーバーレイ。SteamVR にオーバーレイ型でつながっている間だけ使う。
 */
class HudOverlay {
public:
    /** お知らせを出しておく秒数（消え始めるまで）。 */
    static constexpr double kToastHoldSec = 2.2;
    /** お知らせが消えるまでの秒数。 */
    static constexpr double kToastFadeSec = 0.3;

    /**
     * @param keySuffix オーバーレイのキーの後ろに付ける文字列（--test-action では、常駐している本体のキーと
     *                  ぶつからないよう PID を付ける。常駐では空）
     */
    explicit HudOverlay(std::string keySuffix = "") : keySuffix_(std::move(keySuffix)) {}
    HudOverlay(const HudOverlay&) = delete;
    HudOverlay& operator=(const HudOverlay&) = delete;

    /**
     * お知らせを出す（出ていれば中身を差し替えて、時間を数え直す）。
     * @param rgba HudRenderer::renderToast() の画像
     * @param now 今の時刻（秒、単調増加）
     * @return 出せたら true
     */
    bool showToast(const std::vector<uint8_t>& rgba, double now);

    /** 出ているお知らせをすぐ隠す（スクリーンショットに写り込まないように）。 */
    void hideToast();

    /**
     * ミュート中の表示を出す・消す。
     * @param rgba HudRenderer::renderBadge() の画像。nullptr なら消す
     * @return うまくいったら true
     */
    bool setBadge(const std::vector<uint8_t>* rgba);

    /**
     * お知らせの時間を進める（薄くする・隠す）。
     * @param now 今の時刻（秒、単調増加）
     */
    void tick(double now);

    /** @return 次に tick() を呼ぶ時刻（予定がなければ無限大） */
    double nextTickAt() const { return nextTickAt_; }

    /** VR_Shutdown の前に呼ぶ: 隠す → テクスチャを外す → オーバーレイを消す → コンポジタの数フレーム分待つ。 */
    void releaseOverlays();

    /** VR_Shutdown の後に呼ぶ: Vulkan の画像とデバイスを壊す。 */
    void releaseVulkan();

private:
    /**
     * Vulkan・オーバーレイ・テクスチャを作る（作ってあれば何もしない）。
     * @return 使えるなら true
     */
    bool ensure();

    std::string keySuffix_;
    bool tried_ = false;    ///< 作ろうとしたか（失敗したらこの接続の間は作り直さない）
    bool ready_ = false;
    uint64_t toastHandle_ = 0;
    uint64_t badgeHandle_ = 0;
    bool toastVisible_ = false;
    bool badgeVisible_ = false;
    double toastShownAt_ = 0.0;
    double nextTickAt_ = std::numeric_limits<double>::infinity();
    VulkanContext vulkan_;
    OverlayTexture toastTexture_;
    OverlayTexture badgeTexture_;
};
