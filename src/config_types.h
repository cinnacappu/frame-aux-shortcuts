// 設定と画面の両方で使う小さな型（循環の include を避けるため、ここだけに置く）。
#pragma once

/** 表示の言語。 */
enum class Language { Ja, En };

/**
 * 押し方に割り当てるアクションの種類。
 * 種類を足すときは、ここ・config.cpp の kActionSpecs・i18n.cpp の actionLabel・icons.cpp の iconForAction・
 * hud_message.cpp・actions.cpp の prepareAction に 1 行ずつ足す（設定パネルの選択肢は kActionSpecs の順に並ぶ）。
 */
enum class ActionType {
    None,               ///< 何もしない
    MicMuteToggle,      ///< マイクのミュート切り替え（wpctl set-mute）
    BrightnessCycle,    ///< 明るさを brightness_levels の順に切り替え（steamvr/analogGain）
    RoomViewToggle,     ///< パススルー（room view）の切り替え
    Screenshot,         ///< スクリーンショット（IVRScreenshots::RequestScreenshot で VR の画面を PNG に）
    PerfOverlayToggle,  ///< frame-perf-overlay の表示切り替え（SIGUSR1）
    DashboardOpen,      ///< ダッシュボードを開く（IVROverlay::ShowDashboard）
    OriginalAux,        ///< aux の元の動き（KEY_SELECT の押して離すを gpio-keys に書いて SteamVR に届ける。keymap.h）
    Command,            ///< argv をそのまま実行（シェルは通さない）
};
