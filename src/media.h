// Frame の上で流している音楽・動画の操作（MPRIS）のうち、D-Bus を使わない部分: 操作するアプリの選び方と、
// 呼ぶメソッドの決め方。D-Bus でのやりとりは本体の media_control.h（sd-bus）が行う。単体テストできる。
#pragma once

#include "config_types.h"

#include <string>
#include <vector>

/** MPRIS のバス名の頭（"org.mpris.MediaPlayer2.chromium.instance2" など）。 */
constexpr const char* kMprisPrefix = "org.mpris.MediaPlayer2.";
/** playerctld（ほかのアプリをまねるだけの中継）のバス名。同じアプリを 2 回数えないよう、選ぶ候補に入れない。 */
constexpr const char* kPlayerctldName = "org.mpris.MediaPlayer2.playerctld";

/** 音楽・動画の操作の結果の値（ActionOutcome::value・PressRecord::value）。 */
constexpr const char* kMediaPlaying = "playing";    ///< 再生にした
constexpr const char* kMediaPaused = "paused";      ///< 一時停止にした
constexpr const char* kMediaNext = "next";          ///< 次の曲にした
constexpr const char* kMediaPrevious = "previous";  ///< 前の曲にした

/** 失敗の理由のコード（ActionOutcome::reason）。 */
constexpr const char* kReasonNoPlayer = "no_player";            ///< 再生中・一時停止中のアプリが無い
constexpr const char* kReasonMediaUnsupported = "media_unsupported";  ///< 選んだアプリがその操作に対応していない

/** 1 つのアプリ（MPRIS のプレーヤー）の様子。 */
struct MediaPlayer {
    std::string busName;        ///< バス名（"org.mpris.MediaPlayer2.chromium.instance2" など）
    std::string status;         ///< PlaybackStatus: "Playing" / "Paused" / "Stopped"（読めなければ空）
    bool canControl = true;     ///< CanControl（false なら操作できない）
    bool canPlay = false;       ///< CanPlay
    bool canPause = false;      ///< CanPause
    bool canGoNext = false;     ///< CanGoNext
    bool canGoPrevious = false; ///< CanGoPrevious
    std::string title;          ///< Metadata の xesam:title（無ければ空）
};

/** 前に操作したアプリ（同じタブを行き来できるよう、作業用スレッドで覚えておく）。 */
struct MediaMemory {
    std::string lastControlled;  ///< 最後に操作したアプリのバス名
    std::string lastPaused;      ///< 最後に一時停止にしたアプリのバス名（再生し直すときに選ぶ）
};

/**
 * 音楽・動画の操作のアクションか。
 * @param type 種類
 * @return media_play_pause / media_next / media_previous なら true
 */
bool isMediaAction(ActionType type);

/**
 * 操作する候補にするバス名か（MPRIS の名前で、playerctld ではない）。
 * @param name バス名
 * @return 候補にするなら true
 */
bool isMediaPlayerName(const std::string& name);

/**
 * 操作するアプリを選ぶ。CanControl が false のものと、再生中・一時停止中でないもの（Stopped など）は選ばない。
 * 1. 再生中のもの（いくつかあれば、前に操作したもの。無ければ並びの最初）
 * 2. 前に一時停止にしたもの（一時停止中なら。再生・一時停止で同じタブに戻れるように）
 * 3. 一時停止中のもの（前に操作したもの。無ければ並びの最初）
 * @param players アプリの一覧（呼ぶ側がバス名の順に並べておく）
 * @param memory 前に操作したアプリ
 * @return 選んだ番号（無ければ -1）
 */
int chooseMediaPlayer(const std::vector<MediaPlayer>& players, const MediaMemory& memory);

/** 選んだアプリに呼ぶメソッドと、結果の値。 */
struct MediaCall {
    std::string method;  ///< org.mpris.MediaPlayer2.Player のメソッド（"Pause" / "Play" / "Next" / "Previous"）。空なら対応していない
    std::string value;   ///< 結果の値（kMediaPaused など）
};

/**
 * 呼ぶメソッドを決める。再生・一時停止は、今の状態を見て Pause か Play をはっきり呼ぶ（結果の値が分かるように）。
 * @param type 種類（media_play_pause / media_next / media_previous）
 * @param player 選んだアプリ
 * @return メソッドと結果の値（Can… が false なら method は空）
 */
MediaCall mediaCallFor(ActionType type, const MediaPlayer& player);

/**
 * 操作できたあとに、覚えておくアプリを書き換える。
 * @param memory 書き換える先
 * @param busName 操作したアプリのバス名
 * @param value 結果の値（kMediaPaused なら lastPaused にも書く）
 */
void rememberMediaPlayer(MediaMemory& memory, const std::string& busName, const std::string& value);
