// 本体が書き、設定パネルが読む状態ファイル（$XDG_RUNTIME_DIR/frame-aux-shortcuts/status.json）。
// 文言は入れない（言語によらない値だけ）。表示する側が自分の言語で文言にする。
#pragma once

#include "config_types.h"

#include <string>

/** いっしょに使うアプリの状態。 */
enum class AppState {
    Unknown,  ///< まだ調べていない
    Running,  ///< 動いている
    Stopped,  ///< 入っているが動いていない
    Missing,  ///< 入っていない
};

/** 最後に確定した押し方と、その結果。 */
struct PressRecord {
    enum class Kind {
        None,     ///< まだ押していない
        Count,    ///< 1〜5 回押し
        Long,     ///< 長押し
        TooMany,  ///< 6 回以上（何もしない）
    };
    Kind kind = Kind::None;
    int count = 0;                        ///< 回数（Count・TooMany）
    ActionType action = ActionType::None; ///< 割り当ててあったアクション
    bool ok = true;                       ///< 成功したか
    std::string value;   ///< 結果の値: "muted" / "unmuted"、明るさの %、"on" / "off"、"shown" / "hidden"（無ければ空）
    std::string reason;  ///< 失敗の理由: "no_steamvr" / "room_view_api" / "no_command" / "app_missing" / "app_stopped" / "failed"
    std::string detail;  ///< ログ用の説明（日本語）
    long long at = 0;    ///< 確定した時刻（Unix 時間の秒）

    /**
     * 押し方の番号。
     * @return 0〜4 が 1〜5 回押し、5 が長押し、それ以外は -1
     */
    int slot() const;
};

/** 状態ファイルの中身。分からない値は -1。 */
struct Status {
    std::string version;        ///< 本体の版
    int pid = 0;                ///< 本体の PID
    bool active = false;        ///< aux を置き換えている（ショートカットが効く）
    bool vrConnected = false;   ///< SteamVR につながっている
    PressRecord lastPress;
    int micMuted = -1;          ///< マイクのミュート（1 / 0 / -1）
    int brightnessPercent = -1; ///< 明るさ（スライダーの %）
    int roomView = -1;          ///< パススルー（1 / 0 / -1）
    int perfVisible = -1;       ///< frame-perf-overlay の表示（1 / 0 / -1）
    AppState perfOverlay = AppState::Unknown;  ///< frame-perf-overlay
    long long updatedAt = 0;    ///< 書いた時刻（Unix 時間の秒）
};

/**
 * 状態を JSON にする。
 * @param status 状態
 * @return JSON（最後に改行）
 */
std::string statusToJson(const Status& status);

/**
 * 状態の JSON を読む。
 * @param text JSON
 * @param status 読めたときの書き込み先
 * @param error 読めなかったときの理由
 * @return 読めたら true
 */
bool parseStatusText(const std::string& text, Status& status, std::string& error);

/**
 * 状態ファイルを読む。
 * @param path ファイルのパス
 * @param status 読めたときの書き込み先
 * @return 読めたら true
 */
bool loadStatusFile(const std::string& path, Status& status);

/**
 * アプリの状態の名前（状態ファイルでの書き方）。
 * @param state 状態
 * @return "running" / "stopped" / "missing" / "unknown"
 */
const char* appStateName(AppState state);
