// 設定と画面の両方で使う小さな型（循環の include を避けるため、ここだけに置く）。
#pragma once

/** 表示の言語。 */
enum class Language { Ja, En };

/**
 * 押し方に割り当てるアクションの種類。
 * 種類を足すときは、ここ・config.cpp の kActionSpecs・i18n.cpp の actionLabel・icons.cpp の iconForAction・
 * hud_message.cpp・actions.cpp の prepareAction に 1 行ずつ足す（設定パネルの選択肢は kActionSpecs の順に並ぶ。
 * 音楽の操作は media.h の isMediaAction にも足す。パネルでは「音楽」の側に並ぶ。目のアクションは eye_actions.h の
 * isEyeAction。パネルでは「目」の側に並ぶ）。
 * 設定ファイル・状態ファイルには名前（kActionSpecs）で書くので、ここの並びの数は外に出ない。
 */
enum class ActionType {
    None,               ///< 何もしない
    MicMuteToggle,      ///< マイクのミュート切り替え（wpctl set-mute）
    BrightnessCycle,    ///< 明るさを brightness_levels の順に切り替え（steamvr/analogGain）
    RoomViewToggle,     ///< パススルー（room view）の切り替え
    Screenshot,         ///< スクリーンショット（IVRScreenshots::RequestScreenshot で VR の画面を PNG に）
    PerfOverlayToggle,  ///< frame-perf-overlay の表示切り替え（SIGUSR1）
    DashboardOpen,      ///< ダッシュボードを開く（IVROverlay::ShowDashboard）
    Recenter,           ///< 視点のリセット（今向いている方を正面にする。IVRChaperone::ResetZeroPose。recenter.h）
    OriginalAux,        ///< aux の元の動き（KEY_SELECT の押して離すを gpio-keys に書いて SteamVR に届ける。keymap.h）
    MediaPlayPause,     ///< Frame で流している音楽・動画の再生・一時停止（MPRIS。media_control.h）
    MediaNext,          ///< 次の曲（MPRIS の Next）
    MediaPrevious,      ///< 前の曲（MPRIS の Previous）
    EyeRecenter,        ///< 目の正面合わせ（frameeyeosc-panel --action recenter。eye_actions.h）
    EyeFit,             ///< 目のフィット（frameeyeosc-panel --action fit。フィットの途中なら止める）
    EyeRelearn,         ///< 普段の目を覚え直す（frameeyeosc-panel --action relearn。目のカメラが約 30 秒で覚え直す）
    Command,            ///< argv をそのまま実行（シェルは通さない）
};
