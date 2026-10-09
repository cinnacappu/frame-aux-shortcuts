// パススルー（room view）のオン・オフを SteamVR に頼む。
//
// 注意: 公開の OpenVR API には room view を切り替える関数が無いので、SteamVR の Web サーバー
// （ws://127.0.0.1:27062）の **非公開の** メールボックスの仕組みを使う。SteamVR の更新で名前や形が変われば
// 動かなくなる（そのときは失敗の音が鳴る）。呼び方を変えるときは sendRoomViewEnabled() だけを差し替える。
#pragma once

#include <cstdint>
#include <string>

/**
 * room view のオン・オフを SteamVR に送る（WebSocket でつないで、メールボックスに送って閉じる）。
 * 待つのは接続と送信のタイムアウト（各 1 秒）まで。イベントループの外（作業用スレッド）から呼ぶ。
 * @param enabled true でオン、false でオフ
 * @param error 失敗したときの理由
 * @return 送れたら true（効いたかどうかは VREvent_RoomViewShown / Hidden で分かる）
 */
bool sendRoomViewEnabled(bool enabled, std::string& error);

/**
 * vrcompositor_systemlayer のメールボックスに送る JSON の文（テスト用に分けてある）。
 * @param enabled オンなら true
 * @return 例: mailbox_send vrcompositor_systemlayer {"type":"set_camera_roomview_enabled","enabled":true}
 */
std::string roomViewMailboxMessage(bool enabled);

/**
 * クライアントから送る WebSocket のテキストフレーム（FIN 付き・マスク付き）を作る（RFC 6455）。
 * @param text 送る文字列（UTF-8、65535 バイトまで）
 * @param mask マスクの 4 バイト
 * @return フレームのバイト列。長すぎれば空
 */
std::string buildMaskedTextFrame(const std::string& text, const uint8_t mask[4]);
