// キーボードの入力の組み立ての実装。
#include "keyboard_text.h"

#include <cstdio>
#include <cstdlib>

namespace {

/**
 * UTF-8 の先頭のバイトから、その文字のバイト数を決める。
 * @param c 先頭のバイト
 * @return 1〜4。続きのバイト（10xxxxxx）や不正な値なら 0
 */
size_t utf8Length(unsigned char c) {
    if (c < 0x80) return 1;
    if ((c & 0xE0) == 0xC0) return 2;
    if ((c & 0xF0) == 0xE0) return 3;
    if ((c & 0xF8) == 0xF0) return 4;
    return 0;
}

/**
 * UTF-8 の続きのバイトか。
 * @param c バイト
 * @return 10xxxxxx なら true
 */
bool isContinuation(unsigned char c) {
    return (c & 0xC0) == 0x80;
}

/** エスケープの並びの長さの上限（これを超えたら壊れた並びとして捨てる）。 */
constexpr size_t kMaxEscape = 8;

}  // namespace

void KeyboardText::reset(const std::string& initial) {
    text_ = initial;
    cursor_ = text_.size();
    pending_.clear();
    escape_.clear();
}

void KeyboardText::setCursor(size_t byteIndex) {
    if (byteIndex >= text_.size()) {
        cursor_ = text_.size();
        return;
    }
    while (byteIndex > 0 && isContinuation(static_cast<unsigned char>(text_[byteIndex]))) --byteIndex;
    cursor_ = byteIndex;
}

void KeyboardText::left() {
    if (cursor_ == 0) return;
    --cursor_;
    while (cursor_ > 0 && isContinuation(static_cast<unsigned char>(text_[cursor_]))) --cursor_;
}

void KeyboardText::right() {
    if (cursor_ >= text_.size()) return;
    ++cursor_;
    while (cursor_ < text_.size() && isContinuation(static_cast<unsigned char>(text_[cursor_]))) ++cursor_;
}

void KeyboardText::backspace() {
    if (cursor_ == 0) return;
    const size_t end = cursor_;
    left();
    text_.erase(cursor_, end - cursor_);
}

void KeyboardText::deleteForward() {
    if (cursor_ >= text_.size()) return;
    const size_t start = cursor_;
    right();
    text_.erase(start, cursor_ - start);
    cursor_ = start;
}

void KeyboardText::insert(const std::string& character) {
    text_.insert(cursor_, character);
    cursor_ += character.size();
}

void KeyboardText::runEscape() {
    // escape_ は ESC で始まる。CSI（ESC [ …）か SS3（ESC O …）
    const std::string body = escape_.size() > 1 ? escape_.substr(1) : std::string();
    escape_.clear();
    if (body == "[D") {
        left();
    } else if (body == "[C") {
        right();
    } else if (body == "[H" || body == "[1~" || body == "[7~" || body == "OH") {
        cursor_ = 0;
    } else if (body == "[F" || body == "[4~" || body == "[8~" || body == "OF") {
        cursor_ = text_.size();
    } else if (body == "[3~") {
        deleteForward();
    }
    // 上下の矢印（[A・[B）や、そのほかの並びは無視する
}

bool KeyboardText::feed(const char* input, size_t maxBytes) {
    bool done = false;
    for (size_t i = 0; i < maxBytes && input[i] != '\0'; ++i) {
        const unsigned char c = static_cast<unsigned char>(input[i]);
        if (!escape_.empty()) {
            // エスケープの並びを組み立てる（イベントをまたいでもよい）
            if (escape_.size() == 1) {
                if (c == '[' || c == 'O') {
                    escape_ += static_cast<char>(c);
                } else {
                    escape_.clear();  // ESC だけ（Esc キー）: 捨てて、このバイトはふつうに扱う
                    --i;
                }
                continue;
            }
            escape_ += static_cast<char>(c);
            const bool final = (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || c == '~';
            if (final) {
                runEscape();
            } else if (escape_.size() > kMaxEscape) {
                escape_.clear();
            }
            continue;
        }
        if (c == 0x1B) {
            escape_ = "\x1b";
            pending_.clear();
            continue;
        }
        if (c == '\b' || c == 0x7F) {
            // 1 文字ぶん消す（組み立て途中なら、そのバイトを捨てるだけ）
            if (!pending_.empty()) {
                pending_.clear();
                continue;
            }
            backspace();
            continue;
        }
        if (c == '\n' || c == '\r') {
            pending_.clear();
            done = true;
            continue;
        }
        if (c < 0x20) continue;  // そのほかの制御文字は入れない
        if (pending_.empty()) {
            const size_t length = utf8Length(c);
            if (length == 0) continue;  // 先頭が続きのバイト（壊れた入力）
            if (length == 1) {
                insert(std::string(1, static_cast<char>(c)));
                continue;
            }
            pending_ += static_cast<char>(c);
            continue;
        }
        if (!isContinuation(c)) {
            // 続きのはずが新しい文字の先頭: 途中のバイトは捨てて、この文字から組み立て直す
            pending_.clear();
            --i;
            continue;
        }
        pending_ += static_cast<char>(c);
        if (pending_.size() == utf8Length(static_cast<unsigned char>(pending_[0]))) {
            insert(pending_);
            pending_.clear();
        }
    }
    return done;
}

bool keyboardDebugEnabled() {
    static const bool enabled = [] {
        const char* value = std::getenv("FRAME_AUX_KEYBOARD_DEBUG");
        return value != nullptr && std::string(value) == "1";
    }();
    return enabled;
}

std::string keyboardEventLogLine(const char* queue, const char* name, uint32_t type, uint64_t overlayHandle,
                                 uint64_t selfHandle, uint64_t userValue, const char* input, size_t maxBytes, bool debug) {
    size_t bytes = 0;
    while (bytes < maxBytes && input[bytes] != '\0') ++bytes;
    char buffer[256];
    std::snprintf(buffer, sizeof(buffer), "[キーボード] %s %s(%u) overlayHandle=0x%llx（自分 0x%llx）uUserValue=%llu 入力 %zu バイト",
                  queue, name, type, static_cast<unsigned long long>(overlayHandle),
                  static_cast<unsigned long long>(selfHandle), static_cast<unsigned long long>(userValue), bytes);
    std::string line = buffer;
    if (debug) line += " cNewInput=" + hexBytes(input, maxBytes);  // 打った中身（FRAME_AUX_KEYBOARD_DEBUG=1 のときだけ）
    return line + "\n";
}

std::string hexBytes(const char* input, size_t maxBytes) {
    std::string out;
    for (size_t i = 0; i < maxBytes && input[i] != '\0'; ++i) {
        char buffer[4];
        std::snprintf(buffer, sizeof(buffer), "%02x", static_cast<unsigned char>(input[i]));
        if (!out.empty()) out += ' ';
        out += buffer;
    }
    return out.empty() ? "(空)" : out;
}
