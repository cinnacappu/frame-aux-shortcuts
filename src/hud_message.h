// 押したときに視界に出すお知らせの中身（文言・アイコン・色）を、押し方の結果から決める。描画も OpenVR も使わない。
#pragma once

#include "config_types.h"
#include "icons.h"
#include "status.h"

#include <string>

/** お知らせ 1 つの中身。 */
struct HudToast {
    /** 左の丸の色。 */
    enum class Tone {
        Neutral,  ///< 灰色
        Accent,   ///< ピンク
        Success,  ///< 緑
        Danger,   ///< 赤
    };
    bool error = false;          ///< 失敗（赤い枠・「!」）
    Icon icon = Icon::NoEntry;   ///< 左の丸のアイコン（error のときは使わない）
    Tone tone = Tone::Neutral;
    std::string title;           ///< 1 行目（太字）
    std::string sub;             ///< 2 行目（押し方。失敗なら理由も）
};

/**
 * 押し方の結果からお知らせを作る。何もしなかった（none・6 回以上）と、ダッシュボードが開いたとき
 * （開いた画面そのものが手応えになる）、目の正面合わせ・目のフィットを始めたとき（frameeyeosc の点を見てもらう）は
 * 出さない（普段の目を覚え直すは、点を見ないので始めたときも出す）。音楽の操作は、曲名が分かれば 2 行目を曲名にする。
 * @param press 押し方の結果
 * @param language 言語
 * @param toast 出すときの書き込み先
 * @return 出すなら true
 */
bool makeHudToast(const PressRecord& press, Language language, HudToast& toast);

/**
 * カーソルのときに aux を SteamVR に返した・ショートカットに戻したときのお知らせ（設定 original_aux_on_head_pointer）。
 * @param handedBack 返したなら true、ショートカットに戻したなら false
 * @param language 言語
 * @return お知らせ
 */
HudToast makeHandBackToast(bool handedBack, Language language);

/**
 * 失敗の理由のコードを文言にする。
 * @param language 言語
 * @param reason "no_steamvr" / "room_view_api" / "app_missing" / "app_stopped" / "no_command" / "no_pc" / "no_player" /
 *               "media_unsupported" / "not_worn" / "not_tracking" / 目のアクションの "eye_…"（eye_actions.h）/ それ以外
 * @return 文言
 */
std::string reasonText(Language language, const std::string& reason);
