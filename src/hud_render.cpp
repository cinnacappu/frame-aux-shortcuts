// 視界の表示の描画の実装。
#include "hud_render.h"

#include "i18n.h"
#include "theme.h"

#include <algorithm>
#include <cmath>

namespace {

/**
 * 左の丸の地の色と、アイコンの色。
 * @param tone 色の種類
 * @param fill 地の書き込み先
 * @param fg アイコンの書き込み先
 */
void toneColors(HudToast::Tone tone, Color& fill, Color& fg) {
    switch (tone) {
        case HudToast::Tone::Accent:
            fill = kAccentTint;
            fg = kAccent;
            return;
        case HudToast::Tone::Success:
            fill = kSuccessTint;
            fg = kSuccess;
            return;
        case HudToast::Tone::Danger:
            fill = kDangerTint;
            fg = kDanger;
            return;
        case HudToast::Tone::Neutral:
            fill = kIconBox;
            fg = kText;
            return;
    }
}

/**
 * 1 行目と 2 行目の文字の大きさを、幅に収まるよう決める。
 * @param pen 描画の道具
 * @param toast 中身
 * @param style 寸法
 * @param maxTextW 文字に使える幅
 * @param titleSize 1 行目の大きさの書き込み先
 * @param subSize 2 行目の大きさの書き込み先
 */
void fitToastText(const Pen& pen, const HudToast& toast, const ToastStyle& style, double maxTextW, double& titleSize,
                  double& subSize) {
    titleSize = fitSize(pen, toast.title, toast.error ? style.errorTitleSize : style.titleSize, 14, maxTextW, true);
    subSize = toast.sub.empty() ? 0 : fitSize(pen, toast.sub, style.subSize, 11, maxTextW, false);
}

}  // namespace

double measureToast(const Pen& pen, const HudToast& toast, const ToastStyle& style) {
    const double titleSize = toast.error ? style.errorTitleSize : style.titleSize;
    double textW = pen.measure(toast.title, titleSize, true);
    if (!toast.sub.empty()) textW = std::max(textW, pen.measure(toast.sub, style.subSize, false));
    return style.padLeft + style.iconDiameter + style.gap + textW + style.padRight;
}

void drawToast(const Pen& pen, const HudToast& toast, const ToastStyle& style, double x, double y, double maxWidth) {
    cairo_t* cr = pen.cr;
    const double fixedW = style.padLeft + style.iconDiameter + style.gap + style.padRight;
    double titleSize = 0;
    double subSize = 0;
    fitToastText(pen, toast, style, maxWidth - fixedW, titleSize, subSize);
    double textW = pen.measure(toast.title, titleSize, true);
    if (!toast.sub.empty()) textW = std::max(textW, pen.measure(toast.sub, subSize, false));
    const double w = fixedW + textW;
    const double h = style.height;

    // ピル: ふつうは濃い灰色の地に灰色の枠、失敗は赤みの地に赤い枠
    pen.color(toast.error ? kDangerFill : kCard);
    pen.roundedRect(x, y, w, h, h / 2);
    cairo_fill(cr);
    strokeRounded(pen, x, y, w, h, h / 2, toast.error ? kDangerBorder : kBorder, style.border);

    // 左の丸
    Color fill = kIconBox;
    Color fg = kText;
    toneColors(toast.tone, fill, fg);
    const double r = style.iconDiameter / 2;
    const double cx = x + style.padLeft + r;
    const double cy = y + h / 2;
    drawDisc(cr, cx, cy, r, fill);
    if (toast.error) {
        const double size = style.iconDiameter * 0.56;
        textCentered(pen, cx, centerBaseline(cy - r, style.iconDiameter, size), "!", size, fg, true);
    } else {
        drawIcon(cr, toast.icon, cx, cy, style.iconSize, fg);
    }

    // 文字（1 行目は line-height 1.3、2 行目は 1.45 で、2 行をまとめて縦の真ん中に置く）
    const double textX = x + style.padLeft + style.iconDiameter + style.gap;
    if (toast.sub.empty()) {
        pen.text(textX, centerBaseline(y, h, titleSize), toast.title, titleSize, kText, true);
        return;
    }
    const double titleLine = titleSize * 1.3;
    const double subLine = subSize * 1.45;
    const double top = y + (h - titleLine - subLine) / 2;
    pen.text(textX, centerBaseline(top, titleLine, titleSize), toast.title, titleSize, kText, true);
    pen.text(textX, centerBaseline(top + titleLine, subLine, subSize), toast.sub, subSize,
             toast.error ? kDangerSub : kTextMuted, false);
}

double measureBadge(const Pen& pen, const std::string& text, const BadgeStyle& style) {
    return style.padLeft + style.iconSize + style.gap + pen.measure(text, style.textSize, true) + style.padRight;
}

void drawBadge(const Pen& pen, const std::string& text, const BadgeStyle& style, double x, double y) {
    const double w = measureBadge(pen, text, style);
    const double h = style.height;
    pen.color(kDangerTint);
    pen.roundedRect(x, y, w, h, h / 2);
    cairo_fill(pen.cr);
    drawIcon(pen.cr, Icon::MicOff, x + style.padLeft + style.iconSize / 2, y + h / 2, style.iconSize, kDanger, 2.2);
    pen.text(x + style.padLeft + style.iconSize + style.gap, centerBaseline(y, h, style.textSize), text,
             style.textSize, kDanger, true);
}

HudRenderer::HudRenderer(const FontSet& fonts) : fonts_(fonts) {
    toastSurface_ = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, kToastW, kToastH);
    badgeSurface_ = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, kBadgeW, kBadgeH);
}

HudRenderer::~HudRenderer() {
    cairo_surface_destroy(toastSurface_);
    cairo_surface_destroy(badgeSurface_);
}

/**
 * 透明にしてから、倍率をかけた cairo を作る。
 * @param surface 描く画像
 * @return cairo（呼び出し側で cairo_destroy する）
 */
static cairo_t* beginScaled(cairo_surface_t* surface) {
    cairo_t* cr = cairo_create(surface);
    cairo_font_options_t* options = cairo_font_options_create();
    cairo_font_options_set_antialias(options, CAIRO_ANTIALIAS_GRAY);
    cairo_font_options_set_hint_style(options, CAIRO_HINT_STYLE_SLIGHT);
    cairo_set_font_options(cr, options);
    cairo_font_options_destroy(options);
    cairo_set_operator(cr, CAIRO_OPERATOR_CLEAR);
    cairo_paint(cr);
    cairo_set_operator(cr, CAIRO_OPERATOR_OVER);
    cairo_scale(cr, HudRenderer::kScale, HudRenderer::kScale);
    return cr;
}

const std::vector<uint8_t>& HudRenderer::renderToast(const HudToast& toast) {
    cairo_t* cr = beginScaled(toastSurface_);
    const Pen pen {cr, &fonts_};
    const ToastStyle style;
    const double maxW = kToastCssW - 8;
    const double w = std::min(measureToast(pen, toast, style), maxW);
    drawToast(pen, toast, style, (kToastCssW - w) / 2, (kToastCssH - style.height) / 2, maxW);
    cairo_destroy(cr);
    surfaceToRgba(toastSurface_, toastRgba_);
    return toastRgba_;
}

const std::vector<uint8_t>& HudRenderer::renderBadge(Language language) {
    cairo_t* cr = beginScaled(badgeSurface_);
    const Pen pen {cr, &fonts_};
    const BadgeStyle style;
    const std::string text = uiText(language).badgeText;
    const double w = measureBadge(pen, text, style);
    drawBadge(pen, text, style, (kBadgeCssW - w) / 2, (kBadgeCssH - style.height) / 2);
    cairo_destroy(cr);
    surfaceToRgba(badgeSurface_, badgeRgba_);
    return badgeRgba_;
}

bool HudRenderer::writeToastPng(const std::string& path) const {
    return cairo_surface_write_to_png(toastSurface_, path.c_str()) == CAIRO_STATUS_SUCCESS;
}

bool HudRenderer::writeBadgePng(const std::string& path) const {
    return cairo_surface_write_to_png(badgeSurface_, path.c_str()) == CAIRO_STATUS_SUCCESS;
}
