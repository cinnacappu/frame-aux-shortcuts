// 設定パネルの見た目の状態のファイルの実装。
#include "panel_state.h"

#include "json.h"
#include "runtime.h"

#include <sys/stat.h>

#include <cerrno>
#include <cstdlib>

std::string panelStatePath() {
    const char* xdg = std::getenv("XDG_STATE_HOME");
    std::string base;
    if (xdg != nullptr && xdg[0] == '/') {
        base = xdg;
    } else {
        // HOME が無い（か / で始まらない）ときは覚えない（今のフォルダの下には書かない）
        const char* home = std::getenv("HOME");
        if (home == nullptr || home[0] != '/') return "";
        base = std::string(home) + "/.local/state";
    }
    return base + "/frame-aux-shortcuts/panel.json";
}

bool loadPanelTab(const std::string& path, std::string& tab) {
    struct stat st {};
    if (path.empty() || ::lstat(path.c_str(), &st) != 0 || !S_ISREG(st.st_mode)) return false;
    if (st.st_size > kMaxPanelStateBytes) return false;  // 大きすぎるものは読まない
    std::string text;
    if (!readTextFile(path, text)) return false;
    JsonValue root;
    std::string error;
    if (!parseJson(text, root, error) || !root.isObject()) return false;
    const JsonValue* value = root.get("tab");
    if (value == nullptr || !value->isString()) return false;
    tab = value->text;
    return true;
}

bool savePanelTab(const std::string& path, const std::string& tab, std::string& error) {
    if (path.empty()) {
        error = "HOME が無いので、タブを覚えません";
        return false;
    }
    for (const char c : tab) {
        const bool ok = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_';
        if (!ok) {
            error = "タブの名前に使えない文字があります";
            return false;
        }
    }
    // 親のフォルダ（~/.local/state など）は、無ければ 0700 で作る（あればそのまま。中のアプリのフォルダは
    // writeFileAtomically が 0700 で作って、自分だけのものかを確かめる）
    const size_t appSlash = path.find_last_of('/');
    if (appSlash != std::string::npos && appSlash > 0) {
        const std::string appDir = path.substr(0, appSlash);
        const size_t parentSlash = appDir.find_last_of('/');
        if (parentSlash != std::string::npos && parentSlash > 0) {
            const std::string parent = appDir.substr(0, parentSlash);
            const size_t grandSlash = parent.find_last_of('/');
            if (grandSlash != std::string::npos && grandSlash > 0) ::mkdir(parent.substr(0, grandSlash).c_str(), 0700);
            if (::mkdir(parent.c_str(), 0700) != 0 && errno != EEXIST) {
                error = parent + " を作れません";
                return false;
            }
        }
    }
    return writeFileAtomically(path, "{\n  \"version\": 1,\n  \"tab\": \"" + tab + "\"\n}\n", error);
}
