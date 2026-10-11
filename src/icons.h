// 線のアイコン（24×24 の格子の SVG と同じ形）を cairo で描く。フォントに頼らない。
#pragma once

#include "config_types.h"
#include "draw.h"

/** アイコンの種類。 */
enum class Icon {
    Mic,       ///< マイク
    MicOff,    ///< マイク（斜線つき）
    Sun,       ///< 明るさ
    Eye,       ///< パススルー
    Camera,    ///< スクリーンショット
    Chart,     ///< パフォーマンス表示
    Grid,      ///< ダッシュボード
    Terminal,  ///< コマンド
    NoEntry,   ///< なし（丸に斜線）
    Loop,      ///< 1 段目に戻る（回る矢印）
    Monitor,   ///< PC（画面に送る印。VRChat のミュートのプリセット）
    SpeakerOff,///< スピーカー（× つき。スピーカーのミュートのプリセット）
    Pointer,   ///< カーソルの矢印（aux の元の動き。カーソルでクリック）
    PlayPause, ///< 再生・一時停止（三角と 2 本の棒）
    Play,      ///< 再生（三角。再生にしたお知らせ）
    Pause,     ///< 一時停止（2 本の棒。一時停止にしたお知らせ）
    SkipNext,  ///< 次の曲（三角と棒）
    SkipPrevious,///< 前の曲（棒と左向きの三角）
    Recenter,  ///< 視点のリセット（照準: 丸と 4 本の目盛りと真ん中の小さな丸）
};

/**
 * アクションの種類に合うアイコン。
 * @param type 種類
 * @param muted マイクがミュート中なら true（MicOff にする）
 * @return アイコン
 */
Icon iconForAction(ActionType type, bool muted = true);

/**
 * アイコンを描く（線の太さは 24 の格子で strokeWidth）。
 * @param cr cairo
 * @param icon 種類
 * @param cx 真ん中の x
 * @param cy 真ん中の y
 * @param size 一辺（px）
 * @param color 色
 * @param strokeWidth 線の太さ（24 の格子の単位。ふつうは 2、ミュート中の表示は 2.2）
 */
void drawIcon(cairo_t* cr, Icon icon, double cx, double cy, double size, Color color, double strokeWidth = 2.0);
