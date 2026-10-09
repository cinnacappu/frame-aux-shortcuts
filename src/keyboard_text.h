// SteamVR のキーボード（KeyboardFlag_Minimal）から 1 文字ずつ届く入力（VREvent_KeyboardCharInput の cNewInput）を、
// 自分で文字列に組み立てる（入力位置つき）。OpenVR を使わないので単体テストできる。
#pragma once

#include <cstdint>
#include <string>

/**
 * キーボードの入力をためる係。入力位置（カーソル）は UTF-8 の文字の境目のバイト位置で持つ。
 * - cNewInput は最大 7 バイトの UTF-8。1 文字が 2 つのイベントに分かれて届いてもつなげる。文字は入力位置に差し込む
 * - "\b"（と 0x7F）は入力位置の前の 1 文字を消す（組み立て途中のバイトがあればそれを捨てる）
 * - "\n" / "\r" は完了（1 行の欄なので改行は入れない）
 * - エスケープの並び（イベントをまたいでも組み立てる）: ESC [ D / C で左・右へ 1 文字、ESC [ H・ESC [ 1 ~・ESC O H で先頭、
 *   ESC [ F・ESC [ 4 ~・ESC O F で末尾、ESC [ 3 ~ で入力位置の後ろの 1 文字を消す。上下の矢印などそのほかは無視
 * - そのほかの制御文字は無視する
 */
class KeyboardText {
public:
    /**
     * 始める（ためる文字列を初期値から始め、入力位置は末尾）。
     * @param initial 初期値（編集のときの今の値）
     */
    void reset(const std::string& initial);

    /**
     * 1 つのイベントの入力を足す。
     * @param input cNewInput（NUL で終わるか、最大 maxBytes バイト）
     * @param maxBytes 読む最大のバイト数（cNewInput は 8）
     * @return 改行（完了）が来たら true
     */
    bool feed(const char* input, size_t maxBytes = 8);

    /** @return ためた文字列（組み立て途中のバイトは含めない） */
    const std::string& text() const { return text_; }

    /** @return 入力位置（text() の中のバイト位置。文字の境目） */
    size_t cursor() const { return cursor_; }

    /**
     * 入力位置を移す（欄をレーザーで押したとき）。文字の途中なら、その文字の先頭に寄せる。末尾より後ろなら末尾。
     * @param byteIndex バイト位置
     */
    void setCursor(size_t byteIndex);

private:
    /** 入力位置の前の 1 文字を消す。 */
    void backspace();
    /** 入力位置の後ろの 1 文字を消す。 */
    void deleteForward();
    /** 入力位置を左へ 1 文字。 */
    void left();
    /** 入力位置を右へ 1 文字。 */
    void right();
    /** 文字を入力位置に差し込む。 */
    void insert(const std::string& character);
    /** たまったエスケープの並びを実行する。 */
    void runEscape();

    std::string text_;
    size_t cursor_ = 0;
    std::string pending_;   ///< 組み立て途中の UTF-8 のバイト
    std::string escape_;    ///< 組み立て途中のエスケープの並び（ESC から。空なら途中ではない）
};

/**
 * バイト列を 16 進にする（ログ用。例: "e3 81 82"）。
 * @param input バイト列（NUL まで、最大 maxBytes）
 * @param maxBytes 最大のバイト数
 * @return 16 進の文字列
 */
std::string hexBytes(const char* input, size_t maxBytes = 8);

/**
 * キーボードの打った中身をログに出してよいか（環境変数 FRAME_AUX_KEYBOARD_DEBUG=1 のときだけ）。
 * コマンドに秘密の値を打つこともあるので、ふだんは中身をジャーナルに残さない。
 * @return 出してよいなら true
 */
bool keyboardDebugEnabled();

/**
 * キーボードのイベント（1200〜1204）のログの 1 行を作る。ふだんはバイト数だけで、中身（cNewInput）は debug のときだけ 16 進で足す。
 * @param queue どちらのキューか（"オーバーレイ" / "全体"）
 * @param name イベントの名前
 * @param type イベントの番号
 * @param overlayHandle イベントの overlayHandle
 * @param selfHandle 自分のオーバーレイのハンドル
 * @param userValue イベントの uUserValue
 * @param input cNewInput（NUL まで、最大 maxBytes）
 * @param maxBytes cNewInput の大きさ
 * @param debug 中身も出すなら true
 * @return 改行つきの 1 行
 * @example
 * keyboardEventLogLine("オーバーレイ", "KeyboardCharInput", 1201, 0, 5, 256, "a", 8, false)
 * // "[キーボード] オーバーレイ KeyboardCharInput(1201) overlayHandle=0x0（自分 0x5）uUserValue=256 入力 1 バイト\n"
 */
std::string keyboardEventLogLine(const char* queue, const char* name, uint32_t type, uint64_t overlayHandle,
                                 uint64_t selfHandle, uint64_t userValue, const char* input, size_t maxBytes, bool debug);
