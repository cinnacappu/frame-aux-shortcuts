// 視界の表示（押したときのお知らせ・ミュート中の表示）の絵を描く。
// 本体がオーバーレイのテクスチャにし、設定パネルの「視界のイメージ」も同じ部品で描く。
#pragma once

#include "draw.h"
#include "hud_message.h"

#include <cstdint>
#include <string>
#include <vector>

/** お知らせのピルの寸法（px。描くときは kScale 倍）。 */
struct ToastStyle {
    double height = 80;        ///< ピルの高さ
    double iconDiameter = 54;  ///< 左の丸
    double iconSize = 28;      ///< 丸の中のアイコン
    double gap = 16;           ///< 丸と文字の間
    double padLeft = 14;
    double padRight = 28;
    double titleSize = 24;     ///< 1 行目（失敗のときは errorTitleSize）
    double errorTitleSize = 22;
    double subSize = 16;       ///< 2 行目（空なら 1 行だけ）
    double border = 2;
};

/** ミュート中の表示のピルの寸法（px。描くときは kScale 倍）。 */
struct BadgeStyle {
    double height = 44;
    double iconSize = 20;
    double gap = 10;
    double padLeft = 14;
    double padRight = 18;
    double textSize = 18;
};

/**
 * お知らせのピルの幅を測る。
 * @param pen 描画の道具
 * @param toast 中身
 * @param style 寸法
 * @param maxWidth 0 より大きければ、drawToast と同じくこの幅に収めた（文字を小さくし、入らなければ「…」にした）幅
 * @return 幅（px）
 */
double measureToast(const Pen& pen, const HudToast& toast, const ToastStyle& style, double maxWidth = 0);

/**
 * お知らせのピルを描く。
 * @param pen 描画の道具
 * @param toast 中身
 * @param style 寸法
 * @param x 左
 * @param y 上
 * @param maxWidth これより広ければ文字を小さくする（最小の大きさでも入らなければ、後ろを「…」にする。長い曲名など）
 */
void drawToast(const Pen& pen, const HudToast& toast, const ToastStyle& style, double x, double y, double maxWidth);

/**
 * ミュート中の表示のピルの幅を測る。
 * @param pen 描画の道具
 * @param text 文言
 * @param style 寸法
 * @return 幅（px）
 */
double measureBadge(const Pen& pen, const std::string& text, const BadgeStyle& style);

/**
 * ミュート中の表示のピルを描く。
 * @param pen 描画の道具
 * @param text 文言
 * @param style 寸法
 * @param x 左
 * @param y 上
 */
void drawBadge(const Pen& pen, const std::string& text, const BadgeStyle& style, double x, double y);

/**
 * 視界のオーバーレイに渡す画像を作る係（cairo の画像を 2 枚持つ）。画像は寸法の 2 倍の解像度で描く。
 */
class HudRenderer {
public:
    /** 描く倍率（寸法の 1px をテクスチャの何 px にするか）。 */
    static constexpr double kScale = 2.0;
    /** お知らせの画像の大きさ（px）。ピルは横の真ん中に置き、まわりは透明。 */
    static constexpr int kToastCssW = 600;
    static constexpr int kToastCssH = 96;
    /** ミュート中の表示の画像の大きさ（px）。 */
    static constexpr int kBadgeCssW = 280;
    static constexpr int kBadgeCssH = 52;
    static constexpr int kToastW = static_cast<int>(kToastCssW * kScale);
    static constexpr int kToastH = static_cast<int>(kToastCssH * kScale);
    static constexpr int kBadgeW = static_cast<int>(kBadgeCssW * kScale);
    static constexpr int kBadgeH = static_cast<int>(kBadgeCssH * kScale);

    /**
     * @param fonts 使うフォント（このオブジェクトより長く生きていること）
     */
    explicit HudRenderer(const FontSet& fonts);
    ~HudRenderer();
    HudRenderer(const HudRenderer&) = delete;
    HudRenderer& operator=(const HudRenderer&) = delete;

    /**
     * お知らせを描く。
     * @param toast 中身
     * @return 非乗算済み RGBA（kToastW * kToastH * 4 バイト）
     */
    const std::vector<uint8_t>& renderToast(const HudToast& toast);

    /**
     * ミュート中の表示を描く。
     * @param language 言語
     * @return 非乗算済み RGBA（kBadgeW * kBadgeH * 4 バイト）
     */
    const std::vector<uint8_t>& renderBadge(Language language);

    /**
     * 最後に描いたお知らせを PNG で保存する。
     * @param path 保存先
     * @return 保存できたら true
     */
    bool writeToastPng(const std::string& path) const;

    /**
     * 最後に描いたミュート中の表示を PNG で保存する。
     * @param path 保存先
     * @return 保存できたら true
     */
    bool writeBadgePng(const std::string& path) const;

private:
    const FontSet& fonts_;
    cairo_surface_t* toastSurface_ = nullptr;
    cairo_surface_t* badgeSurface_ = nullptr;
    std::vector<uint8_t> toastRgba_;
    std::vector<uint8_t> badgeRgba_;
};
