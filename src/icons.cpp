// 線のアイコンの実装。座標は 24×24 の格子（SVG の viewBox 0 0 24 24 と同じ）。
#include "icons.h"

#include <cmath>

namespace {

/**
 * 角の丸い四角の道筋（SVG の rect rx と同じ）。
 * @param cr cairo
 * @param x 左
 * @param y 上
 * @param w 幅
 * @param h 高さ
 * @param r 角の半径
 */
void roundedRectPath(cairo_t* cr, double x, double y, double w, double h, double r) {
    cairo_new_sub_path(cr);
    cairo_arc(cr, x + w - r, y + r, r, -M_PI / 2, 0);
    cairo_arc(cr, x + w - r, y + h - r, r, 0, M_PI / 2);
    cairo_arc(cr, x + r, y + h - r, r, M_PI / 2, M_PI);
    cairo_arc(cr, x + r, y + r, r, M_PI, 3 * M_PI / 2);
    cairo_close_path(cr);
}

/**
 * 線を 1 本描く道筋を足す。
 * @param cr cairo
 * @param x1 始まり x
 * @param y1 始まり y
 * @param x2 終わり x
 * @param y2 終わり y
 */
void line(cairo_t* cr, double x1, double y1, double x2, double y2) {
    cairo_move_to(cr, x1, y1);
    cairo_line_to(cr, x2, y2);
}

/**
 * マイクの形（本体・U 字・足）。
 * @param cr cairo
 */
void micPath(cairo_t* cr) {
    roundedRectPath(cr, 9, 3, 6, 11, 3);
    // M5 11 a7 7 0 0 0 14 0: 下を回る U 字
    cairo_new_sub_path(cr);
    cairo_arc_negative(cr, 12, 11, 7, M_PI, 0);
    line(cr, 12, 18, 12, 21);
}

/**
 * 目の形（パススルーのアイコンと同じ曲線を、真ん中 (cx, cy)・幅 w に縮めたもの）と瞳。
 * @param cr cairo
 * @param cx 真ん中の x
 * @param cy 真ん中の y
 * @param w 幅（パススルーのアイコンは 20）
 * @param pupil 瞳の半径
 */
void eyePath(cairo_t* cr, double cx, double cy, double w, double pupil) {
    const double k = w / 20.0;
    cairo_move_to(cr, cx - 10 * k, cy);
    cairo_curve_to(cr, cx - 10 * k, cy, cx - 6.4 * k, cy - 7 * k, cx, cy - 7 * k);
    cairo_curve_to(cr, cx + 6.4 * k, cy - 7 * k, cx + 10 * k, cy, cx + 10 * k, cy);
    cairo_curve_to(cr, cx + 10 * k, cy, cx + 6.4 * k, cy + 7 * k, cx, cy + 7 * k);
    cairo_curve_to(cr, cx - 6.4 * k, cy + 7 * k, cx - 10 * k, cy, cx - 10 * k, cy);
    cairo_close_path(cr);
    cairo_new_sub_path(cr);
    cairo_arc(cr, cx, cy, pupil, 0, 2 * M_PI);
}

}  // namespace

Icon iconForAction(ActionType type, bool muted) {
    switch (type) {
        case ActionType::MicMuteToggle: return muted ? Icon::MicOff : Icon::Mic;
        case ActionType::BrightnessCycle: return Icon::Sun;
        case ActionType::RoomViewToggle: return Icon::Eye;
        case ActionType::Screenshot: return Icon::Camera;
        case ActionType::PerfOverlayToggle: return Icon::Chart;
        case ActionType::DashboardOpen: return Icon::Grid;
        case ActionType::Recenter: return Icon::Recenter;
        case ActionType::OriginalAux: return Icon::Pointer;
        case ActionType::MediaPlayPause: return Icon::PlayPause;
        case ActionType::MediaNext: return Icon::SkipNext;
        case ActionType::MediaPrevious: return Icon::SkipPrevious;
        case ActionType::EyeRecenter: return Icon::EyeRecenter;
        case ActionType::EyeFit: return Icon::EyeFit;
        case ActionType::EyeRelearn: return Icon::EyeRelearn;
        case ActionType::Command: return Icon::Terminal;
        case ActionType::None: return Icon::NoEntry;
    }
    return Icon::NoEntry;
}

void drawIcon(cairo_t* cr, Icon icon, double cx, double cy, double size, Color color, double strokeWidth) {
    cairo_save(cr);
    cairo_translate(cr, cx - size / 2, cy - size / 2);
    cairo_scale(cr, size / 24.0, size / 24.0);
    cairo_set_source_rgb(cr, color.r, color.g, color.b);
    cairo_set_line_width(cr, strokeWidth);
    cairo_set_line_cap(cr, CAIRO_LINE_CAP_ROUND);
    cairo_set_line_join(cr, CAIRO_LINE_JOIN_ROUND);
    cairo_new_path(cr);
    switch (icon) {
        case Icon::Mic: micPath(cr); break;
        case Icon::MicOff:
            micPath(cr);
            line(cr, 3, 3, 21, 21);
            break;
        case Icon::Sun: {
            cairo_new_sub_path(cr);
            cairo_arc(cr, 12, 12, 4, 0, 2 * M_PI);
            line(cr, 12, 2, 12, 4);
            line(cr, 12, 20, 12, 22);
            line(cr, 4.9, 4.9, 6.3, 6.3);
            line(cr, 17.7, 17.7, 19.1, 19.1);
            line(cr, 2, 12, 4, 12);
            line(cr, 20, 12, 22, 12);
            line(cr, 4.9, 19.1, 6.3, 17.7);
            line(cr, 17.7, 6.3, 19.1, 4.9);
            break;
        }
        case Icon::Eye: {
            // M2 12 s3.6-7 10-7 10 7 10 7 -3.6 7-10 7 S2 12 2 12z（滑らかな曲線を 3 次ベジェに直したもの）
            cairo_move_to(cr, 2, 12);
            cairo_curve_to(cr, 2, 12, 5.6, 5, 12, 5);
            cairo_curve_to(cr, 18.4, 5, 22, 12, 22, 12);
            cairo_curve_to(cr, 22, 12, 18.4, 19, 12, 19);
            cairo_curve_to(cr, 5.6, 19, 2, 12, 2, 12);
            cairo_close_path(cr);
            cairo_new_sub_path(cr);
            cairo_arc(cr, 12, 12, 3, 0, 2 * M_PI);
            break;
        }
        case Icon::Camera:
            // M23 19 a2 2 0 0 1-2 2 H3 a2 2 0 0 1-2-2 V8 a2 2 0 0 1 2-2 h4 l2-3 h6 l2 3 h4 a2 2 0 0 1 2 2 z ＋ 丸（レンズ）
            cairo_move_to(cr, 23, 19);
            cairo_arc(cr, 21, 19, 2, 0, M_PI / 2);
            cairo_line_to(cr, 3, 21);
            cairo_arc(cr, 3, 19, 2, M_PI / 2, M_PI);
            cairo_line_to(cr, 1, 8);
            cairo_arc(cr, 3, 8, 2, M_PI, 3 * M_PI / 2);
            cairo_line_to(cr, 7, 6);
            cairo_line_to(cr, 9, 3);
            cairo_line_to(cr, 15, 3);
            cairo_line_to(cr, 17, 6);
            cairo_line_to(cr, 21, 6);
            cairo_arc(cr, 21, 8, 2, 3 * M_PI / 2, 2 * M_PI);
            cairo_close_path(cr);
            cairo_new_sub_path(cr);
            cairo_arc(cr, 12, 13, 4, 0, 2 * M_PI);
            break;
        case Icon::Chart:
            cairo_move_to(cr, 3, 3);
            cairo_line_to(cr, 3, 21);
            cairo_line_to(cr, 21, 21);
            cairo_move_to(cr, 7, 15);
            cairo_line_to(cr, 11, 11);
            cairo_line_to(cr, 14, 14);
            cairo_line_to(cr, 19, 8);
            break;
        case Icon::Grid:
            roundedRectPath(cr, 3, 3, 7, 7, 1.5);
            roundedRectPath(cr, 14, 3, 7, 7, 1.5);
            roundedRectPath(cr, 3, 14, 7, 7, 1.5);
            roundedRectPath(cr, 14, 14, 7, 7, 1.5);
            break;
        case Icon::Terminal:
            cairo_move_to(cr, 4, 17);
            cairo_line_to(cr, 10, 12);
            cairo_line_to(cr, 4, 7);
            line(cr, 12, 19, 20, 19);
            break;
        case Icon::NoEntry:
            cairo_new_sub_path(cr);
            cairo_arc(cr, 12, 12, 9, 0, 2 * M_PI);
            line(cr, 5.6, 5.6, 18.4, 18.4);
            break;
        case Icon::Loop: {
            // M3 12 a9 9 0 1 0 3-6.7: 中心 (12,12)・半径 9 の大きな弧を左から反時計回りに。M3 4 v5 h5: 矢じり
            const double end = std::atan2(5.3 - 12.0, 6.0 - 12.0);
            cairo_new_sub_path(cr);
            cairo_arc_negative(cr, 12, 12, 9, M_PI, end);
            cairo_move_to(cr, 3, 4);
            cairo_line_to(cr, 3, 9);
            cairo_line_to(cr, 8, 9);
            break;
        }
        case Icon::Monitor:
            // rect 3,4 18×12 rx2・M8 20h8・M12 16v4・送る印の三角 M10 8.5 l4 1.5 -4 1.5z
            roundedRectPath(cr, 3, 4, 18, 12, 2);
            line(cr, 8, 20, 16, 20);
            line(cr, 12, 16, 12, 20);
            cairo_move_to(cr, 10, 8.5);
            cairo_line_to(cr, 14, 10);
            cairo_line_to(cr, 10, 11.5);
            cairo_close_path(cr);
            break;
        case Icon::SpeakerOff:
            // M4 9h4l5-4v14l-5-4H4z・M17 9l4 6・M21 9l-4 6
            cairo_move_to(cr, 4, 9);
            cairo_line_to(cr, 8, 9);
            cairo_line_to(cr, 13, 5);
            cairo_line_to(cr, 13, 19);
            cairo_line_to(cr, 8, 15);
            cairo_line_to(cr, 4, 15);
            cairo_close_path(cr);
            line(cr, 17, 9, 21, 15);
            line(cr, 21, 9, 17, 15);
            break;
        case Icon::Pointer:
            // M4 4 l7 16 2.5-6.5 6.5-2.5z（矢印）・M13.5 13.5 l6 6（柄）
            cairo_move_to(cr, 4, 4);
            cairo_line_to(cr, 11, 20);
            cairo_line_to(cr, 13.5, 13.5);
            cairo_line_to(cr, 20, 11);
            cairo_close_path(cr);
            line(cr, 13.5, 13.5, 19.5, 19.5);
            break;
        case Icon::PlayPause:
            // M3 5 l9 7 -9 7z（三角）・M16 5v14・M21 5v14（2 本の棒）
            cairo_move_to(cr, 3, 5);
            cairo_line_to(cr, 12, 12);
            cairo_line_to(cr, 3, 19);
            cairo_close_path(cr);
            line(cr, 16, 5, 16, 19);
            line(cr, 21, 5, 21, 19);
            break;
        case Icon::Play:
            // Feather の play: polygon 5 3 19 12 5 21
            cairo_move_to(cr, 5, 3);
            cairo_line_to(cr, 19, 12);
            cairo_line_to(cr, 5, 21);
            cairo_close_path(cr);
            break;
        case Icon::Pause:
            // Feather の pause: rect 6 4 4 16・rect 14 4 4 16
            roundedRectPath(cr, 6, 4, 4, 16, 1);
            roundedRectPath(cr, 14, 4, 4, 16, 1);
            break;
        case Icon::SkipNext:
            // Feather の skip-forward: polygon 5 4 15 12 5 20・M19 5v14
            cairo_move_to(cr, 5, 4);
            cairo_line_to(cr, 15, 12);
            cairo_line_to(cr, 5, 20);
            cairo_close_path(cr);
            line(cr, 19, 5, 19, 19);
            break;
        case Icon::SkipPrevious:
            // Feather の skip-back: polygon 19 20 9 12 19 4・M5 19V5
            cairo_move_to(cr, 19, 20);
            cairo_line_to(cr, 9, 12);
            cairo_line_to(cr, 19, 4);
            cairo_close_path(cr);
            line(cr, 5, 19, 5, 5);
            break;
        case Icon::Recenter:
            // Lucide の locate-fixed: 丸 r7・M2 12h3・M19 12h3・M12 2v3・M12 19v3 ＋ 真ん中の丸 r3（ここでは塗らずに線）
            cairo_new_sub_path(cr);
            cairo_arc(cr, 12, 12, 7, 0, 2 * M_PI);
            cairo_new_sub_path(cr);
            cairo_arc(cr, 12, 12, 2.5, 0, 2 * M_PI);
            line(cr, 2, 12, 5, 12);
            line(cr, 19, 12, 22, 12);
            line(cr, 12, 2, 12, 5);
            line(cr, 12, 19, 12, 22);
            break;
        case Icon::EyeRecenter:
            // Lucide の scan-eye に近い形: 四隅の枠（M3 7V5a2 2 0 0 1 2-2h2 など）＋ 真ん中に小さな目
            cairo_move_to(cr, 3, 7);
            cairo_line_to(cr, 3, 5);
            cairo_arc(cr, 5, 5, 2, M_PI, 3 * M_PI / 2);
            cairo_line_to(cr, 7, 3);
            cairo_move_to(cr, 17, 3);
            cairo_line_to(cr, 19, 3);
            cairo_arc(cr, 19, 5, 2, 3 * M_PI / 2, 2 * M_PI);
            cairo_line_to(cr, 21, 7);
            cairo_move_to(cr, 21, 17);
            cairo_line_to(cr, 21, 19);
            cairo_arc(cr, 19, 19, 2, 0, M_PI / 2);
            cairo_line_to(cr, 17, 21);
            cairo_move_to(cr, 7, 21);
            cairo_line_to(cr, 5, 21);
            cairo_arc(cr, 5, 19, 2, M_PI / 2, M_PI);
            cairo_line_to(cr, 3, 17);
            eyePath(cr, 12, 12, 13, 1.6);
            break;
        case Icon::EyeFit:
            // 目（左下に寄せて少し小さく）と、右上に目で追う点（フィットで出る点）
            eyePath(cr, 10, 14, 16, 2.2);
            cairo_new_sub_path(cr);
            cairo_arc(cr, 19.5, 4.5, 2, 0, 2 * M_PI);
            break;
        case Icon::EyeRelearn:
            // Lucide の refresh-cw に近い、回る 2 本の矢印（半径 9 の弧と角の矢じり）の中に小さな目
            cairo_new_sub_path(cr);
            cairo_arc(cr, 12, 12, 9, M_PI, 2 * M_PI - M_PI / 6);
            cairo_move_to(cr, 20.5, 3);
            cairo_line_to(cr, 20.5, 7.5);
            cairo_line_to(cr, 16, 7.5);
            cairo_new_sub_path(cr);
            cairo_arc(cr, 12, 12, 9, 0, M_PI - M_PI / 6);
            cairo_move_to(cr, 3.5, 21);
            cairo_line_to(cr, 3.5, 16.5);
            cairo_line_to(cr, 8, 16.5);
            eyePath(cr, 12, 12, 9, 1.3);
            break;
    }
    cairo_stroke(cr);
    cairo_restore(cr);
}
