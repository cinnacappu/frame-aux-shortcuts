// 状態ファイルの読み書きの実装。
#include "status.h"

#include "config.h"
#include "json.h"
#include "runtime.h"

#include <cmath>
#include <cstdio>
#include <sstream>

namespace {

/**
 * 文字列を JSON の "..." にする。
 * @param text 文字列
 * @return エスケープ済みの "..."
 */
std::string quote(const std::string& text) {
    std::string out = "\"";
    for (const char c : text) {
        const unsigned char u = static_cast<unsigned char>(c);
        if (c == '"' || c == '\\') {
            out += '\\';
            out += c;
        } else if (u < 0x20) {
            char buffer[8];
            std::snprintf(buffer, sizeof(buffer), "\\u%04x", u);
            out += buffer;
        } else {
            out += c;
        }
    }
    return out + "\"";
}

/**
 * 3 つの値（1 / 0 / -1）を JSON にする。
 * @param value 値
 * @return true / false / null
 */
std::string triState(int value) {
    return value < 0 ? "null" : (value != 0 ? "true" : "false");
}

/**
 * 押し方の種類の名前。
 * @param kind 種類
 * @return "count" / "long" / "too_many" / "none"
 */
const char* kindName(PressRecord::Kind kind) {
    switch (kind) {
        case PressRecord::Kind::Count: return "count";
        case PressRecord::Kind::Long: return "long";
        case PressRecord::Kind::TooMany: return "too_many";
        case PressRecord::Kind::None: return "none";
    }
    return "none";
}

/**
 * 真偽値か null を 3 つの値で読む。
 * @param object オブジェクト
 * @param key キー
 * @return 1 / 0 / -1
 */
int readTriState(const JsonValue& object, const char* key) {
    const JsonValue* value = object.get(key);
    if (value == nullptr || !value->isBool()) return -1;
    return value->boolean ? 1 : 0;
}

/**
 * 文字列を読む（無ければ空）。
 * @param object オブジェクト
 * @param key キー
 * @return 値
 */
std::string readText(const JsonValue& object, const char* key) {
    const JsonValue* value = object.get(key);
    return value != nullptr && value->isString() ? value->text : std::string();
}

/**
 * 数値を読む（無ければ fallback）。
 * @param object オブジェクト
 * @param key キー
 * @param fallback 無いときの値
 * @return 値
 */
long long readNumber(const JsonValue& object, const char* key, long long fallback) {
    const JsonValue* value = object.get(key);
    return value != nullptr && value->isNumber() ? static_cast<long long>(std::llround(value->number)) : fallback;
}

}  // namespace

int PressRecord::slot() const {
    if (kind == Kind::Long) return kLongPressSlot;
    if (kind == Kind::Count && count >= 1 && count <= kMaxPressCount) return count - 1;
    return -1;
}

const char* appStateName(AppState state) {
    switch (state) {
        case AppState::Running: return "running";
        case AppState::Stopped: return "stopped";
        case AppState::Missing: return "missing";
        case AppState::Unknown: return "unknown";
    }
    return "unknown";
}

std::string statusToJson(const Status& s) {
    std::ostringstream out;
    out << "{\n"
        << "  \"version\": " << quote(s.version) << ",\n"
        << "  \"pid\": " << s.pid << ",\n"
        << "  \"active\": " << (s.active ? "true" : "false") << ",\n"
        << "  \"handed_back\": " << (s.handedBack ? "true" : "false") << ",\n"
        << "  \"vr_connected\": " << (s.vrConnected ? "true" : "false") << ",\n";
    if (s.lastPress.kind == PressRecord::Kind::None) {
        out << "  \"last_press\": null,\n";
    } else {
        const PressRecord& p = s.lastPress;
        out << "  \"last_press\": { \"kind\": " << quote(kindName(p.kind)) << ", \"count\": " << p.count
            << ", \"action\": " << quote(actionTypeName(p.action)) << ", \"ok\": " << (p.ok ? "true" : "false")
            << ", \"value\": " << quote(p.value) << ", \"reason\": " << quote(p.reason)
            << ", \"detail\": " << quote(p.detail) << ", \"at\": " << p.at << " },\n";
    }
    out << "  \"mic_muted\": " << triState(s.micMuted) << ",\n"
        << "  \"brightness_percent\": " << (s.brightnessPercent < 0 ? std::string("null") : std::to_string(s.brightnessPercent))
        << ",\n"
        << "  \"room_view\": " << triState(s.roomView) << ",\n"
        << "  \"perf_overlay_visible\": " << triState(s.perfVisible) << ",\n"
        << "  \"apps\": { \"frame-perf-overlay\": " << quote(appStateName(s.perfOverlay)) << " },\n"
        << "  \"updated_at\": " << s.updatedAt << "\n"
        << "}\n";
    return out.str();
}

bool parseStatusText(const std::string& text, Status& status, std::string& error) {
    JsonValue root;
    if (!parseJson(text, root, error)) return false;
    if (!root.isObject()) {
        error = "一番外側が { ... } ではありません";
        return false;
    }
    Status s;
    s.version = readText(root, "version");
    s.pid = static_cast<int>(readNumber(root, "pid", 0));
    s.active = readTriState(root, "active") == 1;
    s.handedBack = readTriState(root, "handed_back") == 1;
    s.vrConnected = readTriState(root, "vr_connected") == 1;
    if (const JsonValue* press = root.get("last_press"); press != nullptr && press->isObject()) {
        PressRecord& p = s.lastPress;
        const std::string kind = readText(*press, "kind");
        p.kind = kind == "count"      ? PressRecord::Kind::Count
                 : kind == "long"     ? PressRecord::Kind::Long
                 : kind == "too_many" ? PressRecord::Kind::TooMany
                                      : PressRecord::Kind::None;
        p.count = static_cast<int>(readNumber(*press, "count", 0));
        if (!parseActionType(readText(*press, "action"), p.action)) p.action = ActionType::None;
        p.ok = readTriState(*press, "ok") != 0;
        p.value = readText(*press, "value");
        p.reason = readText(*press, "reason");
        p.detail = readText(*press, "detail");
        p.at = readNumber(*press, "at", 0);
    }
    s.micMuted = readTriState(root, "mic_muted");
    s.brightnessPercent = static_cast<int>(readNumber(root, "brightness_percent", -1));
    s.roomView = readTriState(root, "room_view");
    s.perfVisible = readTriState(root, "perf_overlay_visible");
    if (const JsonValue* apps = root.get("apps"); apps != nullptr && apps->isObject()) {
        const std::string perf = readText(*apps, "frame-perf-overlay");
        s.perfOverlay = perf == "running"   ? AppState::Running
                        : perf == "stopped" ? AppState::Stopped
                        : perf == "missing" ? AppState::Missing
                                            : AppState::Unknown;
    }
    s.updatedAt = readNumber(root, "updated_at", 0);
    status = s;
    return true;
}

bool loadStatusFile(const std::string& path, Status& status) {
    std::string text;
    if (!readTextFile(path, text)) return false;
    std::string error;
    return parseStatusText(text, status, error);
}
