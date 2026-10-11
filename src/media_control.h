// Frame の上で流している音楽・動画を、セッションバスの MPRIS（org.mpris.MediaPlayer2.Player）で操作する。
// Chrome・Chromium（Flatpak）など、MPRIS に対応したアプリが相手。Steam Link でつないだ PC の音は対象外。
// D-Bus は libsystemd の sd-bus で直接話す（gdbus などを起動しない。曲名の文字コードがロケールに左右されない）。
#pragma once

#include "actions.h"

/**
 * 音楽・動画を操作する（再生・一時停止、次の曲、前の曲）。作業用スレッドで呼ぶ。
 * 1 回の呼び出しは 0.5〜1 秒で打ち切るので、止まったアプリがいても待ち続けない。
 * 次の曲・前の曲のあとは、曲名が変わるまで最大 0.8 秒待つ（変わらなければ曲名は空）。
 * 前に操作したアプリはこの関数の中で覚えておく（chooseMediaPlayer の memory）。
 * @param type media_play_pause / media_next / media_previous
 * @return 結果（value は kMediaPaused など、track は曲名。ログ用の説明には曲名を書かない）
 */
ActionOutcome runMediaAction(ActionType type);
