// gpio-keys（aux ボタンと音量 + があるデバイス）のキー対応表の置き換えと、元への戻し。aux の元の動きの中継もここ。
// EVIOCGRAB はしない（同じデバイスに音量 + があり、掴むと SteamVR に届かなくなる）。
#pragma once

#include <string>
#include <utility>
#include <vector>

/** aux ボタンの元のキーコード（KEY_SELECT）。 */
constexpr unsigned kAuxOriginalCode = 353;
/** 置き換え先のキーコード（KEY_MACRO1）。SteamVR・Steam・gamescope が意味を持たせていないキー。 */
constexpr unsigned kAuxStandInCode = 656;

/**
 * 名前が gpio-keys の /dev/input/event* を探す。
 * @return 見つかったデバイスのパス。無ければ空文字列
 */
std::string findGpioKeysDevice();

/**
 * キー対応表を読む（読むだけ）。
 * @param fd 開いたデバイス
 * @return (index, keycode) の一覧。読めなければ空
 */
std::vector<std::pair<unsigned, unsigned>> readKeymap(int fd);

/**
 * 対応表を "[(0, 115), (1, 353)]" の形の文字列にする（ログと確認用）。
 * @param keymap readKeymap() の結果
 * @return 文字列
 */
std::string formatKeymap(const std::vector<std::pair<unsigned, unsigned>>& keymap);

/**
 * 対応表のうち、キーコードが from の項目をすべて to に置き換える。
 * ioctl だけを使うので、シグナルハンドラーの中からも呼べる（async-signal-safe）。
 * @param fd 開いたデバイス
 * @param from 置き換える前のキーコード
 * @param to 置き換えた後のキーコード
 * @return 置き換えた項目の数。失敗したら -1
 */
int swapKeycode(int fd, unsigned from, unsigned to);

/**
 * 常駐中に対応表を守る役。置き換えを覚えておき、終わるとき・落ちるときに必ず元に戻す。
 * 致命的なシグナル（SIGSEGV・SIGABRT など）のハンドラーもここで入れる。
 */
namespace keymap_guard {

/**
 * gpio-keys を開いて守りを有効にする（まだ置き換えない）。aux（353）か、前回の置き換え（656）があることを確かめる。
 * 成功したら、致命的なシグナルと atexit で元に戻す仕組みが有効になる。
 * @param devicePath gpio-keys のパス
 * @param message 結果の説明（ログ用）
 * @return 読み取り用にも使える fd（非ブロッキング）。失敗なら -1
 */
int attach(const std::string& devicePath, std::string& message);

/**
 * 353 → 656 に置き換える（aux を SteamVR に届かなくする）。すでに 656 なら何もしない。attach() の後に呼ぶ。
 * @param message 結果の説明（ログ用）
 * @return 置き換えた（置き換え済みだった）なら true
 */
bool activate(std::string& message);

/**
 * 656 → 353 に戻して、aux を元の動き（SteamVR）に返す。fd は開いたまま（また activate() できる）。
 * @param message 結果の説明（ログ用）
 * @return 戻せた（置き換えていなかった）なら true
 */
bool deactivate(std::string& message);

/**
 * gpio-keys を開いて、353 → 656 に置き換える（attach() と activate() を続けて呼ぶ）。
 * すでに 656 があれば前回の置き換えとして引き継ぐ。
 * @param devicePath gpio-keys のパス
 * @param message 結果の説明（ログ用）
 * @return 置き換え済み（引き継ぎを含む）の状態になれば、読み取り用にも使える fd。失敗なら -1
 */
int remap(const std::string& devicePath, std::string& message);

/**
 * 656 → 353 に戻す。何度呼んでもよい。致命的なシグナルのハンドラーからも呼ばれる。
 * @return 戻した項目の数。置き換えていなければ 0、失敗したら -1
 */
int restore();

/**
 * aux の元の動き: aux を 1 回押したのと同じ入力（KEY_SELECT の押して離す）を gpio-keys に書いて、SteamVR に届ける。
 * 置き換え中（656）は、デバイスが KEY_SELECT を持っていないので書いても捨てられる。そのため、その間だけ 656 → 353 に戻して書き、
 * 書けても書けなくても 353 → 656 に置き直す（約 80 ms。その間に本物の aux を押すと SteamVR に行く）。
 * 置き換えていなければ、書くだけ。書いた KEY_SELECT は、本体の押し方の読み取り（656 だけを数える）には入らない。
 * attach() していないプロセス（--test-action）では、置き換えの持ち主ではないので、置き換え中なら書かずに失敗にする。
 * メインスレッドから呼ぶ（activate()・deactivate() と同じスレッド）。
 * @param message 結果の説明（ログ用）
 * @return 書けて、置き換えも元どおりなら true
 */
bool pressOriginal(std::string& message);

/**
 * pressOriginal() で置き直せなかったか（aux が SteamVR に届くままかもしれない）。返したら忘れる。
 * 本体はこれを見たら、置き換えていないことにして置き換えをやり直す。メインスレッドから呼ぶ。
 * @return 置き直せなかったことがあれば true
 */
bool takeRemapLost();

/**
 * 致命的なシグナル（SIGSEGV・SIGABRT・SIGBUS・SIGFPE・SIGILL など）で、対応表を戻してから同じシグナルで落ちるようにする。
 * attach() より前に一度呼んでおく。SIGUSR2 は「ユーザーの終了」に使うので入れない。
 */
void installCrashHandlers();

}  // namespace keymap_guard
