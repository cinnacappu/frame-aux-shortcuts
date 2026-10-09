// 押し方が確定したときの手応えの音（成功・失敗の 2 種）。pw-play を子プロセスで鳴らし、終わるのは待たない。
#pragma once

#include <string>

/** 音の設定（Config から写す。作業用スレッドに渡すので値で持つ）。 */
struct SoundSettings {
    bool enabled = true;      ///< false で鳴らさない
    std::string success;      ///< 成功の音のパス（空なら鳴らさない）
    std::string failure;      ///< 失敗の音のパス（空なら鳴らさない）
};

/**
 * 成功・失敗の音を鳴らす（pw-play を起動するだけで、終わるのは待たない。ゾンビは残さない）。
 * @param settings 音の設定
 * @param success 成功なら true
 */
void playFeedbackSound(const SoundSettings& settings, bool success);
