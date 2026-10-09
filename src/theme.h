// 画面の色（設定パネルと視界の表示）と、画面の大きさの決まり。描画コードはここの色だけを使う。
#pragma once

#include "draw.h"

/**
 * 16 進の RGB（例: 0xe07ef5）から色を作る。
 * @param rgb 0xRRGGBB
 * @return 色（0〜1）
 */
constexpr Color hexColor(unsigned rgb) {
    return {((rgb >> 16) & 0xFF) / 255.0, ((rgb >> 8) & 0xFF) / 255.0, (rgb & 0xFF) / 255.0};
}

// ---- 地の段 ----
constexpr Color kBg = hexColor(0x0e1015);           ///< パネルの地
constexpr Color kCard = hexColor(0x1a1d24);         ///< カード・区画
constexpr Color kControl = hexColor(0x20242c);      ///< ボタン・明るさの段のカード・アプリの行
constexpr Color kControlHover = hexColor(0x2b3039); ///< ポインターが乗っている・押している
constexpr Color kIconBox = hexColor(0x262a33);      ///< カードのアイコンの地
constexpr Color kDiagram = hexColor(0x14171d);      ///< 押し方の図の地
constexpr Color kPreview = hexColor(0x2a2e35);      ///< 視界のイメージの地
// ---- 文字 ----
constexpr Color kText = hexColor(0xf2f3f5);         ///< 本文
constexpr Color kTextMuted = hexColor(0xa3a9b4);    ///< 補足
constexpr Color kTextLabel = hexColor(0xc9cdd4);    ///< カードの押し方の名前・押している印
constexpr Color kTextFaint = hexColor(0x8b919c);    ///< 入っていないアプリの選択肢・視界のイメージの説明
constexpr Color kTextDisabled = hexColor(0x5c626d); ///< 押せないボタンの文字
constexpr Color kArrow = hexColor(0x6b7280);        ///< 明るさの段の間の →
// ---- 枠・線 ----
constexpr Color kBorder = hexColor(0x4a505c);       ///< ボタン・ピルの枠
constexpr Color kDivider = hexColor(0x3a3f4a);      ///< 区切り線・点線の枠
constexpr Color kBorderDisabled = hexColor(0x2c3039);  ///< 押せないボタンの枠・明るさの段のカードの枠
// ---- イメージカラー ----
constexpr Color kAccent = hexColor(0xe07ef5);       ///< 選択中
constexpr Color kAccentPressed = hexColor(0xc862de);///< 選択中を押している間
constexpr Color kOnAccent = hexColor(0x1c0f22);     ///< アクセントの塗りの上の文字
constexpr Color kAccentTint = hexColor(0x2d1a33);   ///< 選択中のカードのアイコンの地
// ---- 状態の色 ----
constexpr Color kSuccess = hexColor(0x4ade80);      ///< 動作中
constexpr Color kSuccessTint = hexColor(0x173424);
constexpr Color kDanger = hexColor(0xff8a80);       ///< ミュート中・入っていない（文字・アイコン）
constexpr Color kDangerTint = hexColor(0x3a1a1c);
constexpr Color kDangerBorder = hexColor(0xe5534b); ///< 失敗の枠・終了ボタンの枠
constexpr Color kDangerFill = hexColor(0x2a1518);   ///< 失敗の地・終了ボタンの地
constexpr Color kDangerCardBorder = hexColor(0x4a2326);  ///< 使えない割り当てのカードの枠
constexpr Color kDangerText = hexColor(0xffd2cd);   ///< 更新の確認に失敗したときの文字
constexpr Color kDangerSub = hexColor(0xffb4ac);    ///< 視界の表示の失敗の補足
constexpr Color kStoppedTint = hexColor(0x262a33);  ///< 「止まっている」の地
constexpr Color kInfo = hexColor(0x7aa7ff);        ///< 「Konsole で実行中…」の文字と点
constexpr Color kInfoTint = hexColor(0x1c2740);    ///< その地

// ---- フォント（frame-perf-overlay と同じ。読めなければ fontconfig の Noto Sans CJK JP） ----
constexpr const char* kFontRegular = "/usr/share/fonts/noto-cjk/NotoSansCJK-Regular.ttc";
constexpr const char* kFontBold = "/usr/share/fonts/noto-cjk/NotoSansCJK-Bold.ttc";
