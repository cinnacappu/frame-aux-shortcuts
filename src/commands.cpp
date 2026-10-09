// 登録したコマンドとフォルダのコマンドの実装。
#include "commands.h"

#include <dirent.h>
#include <sys/stat.h>
#include <unistd.h>

#include <algorithm>
#include <cstdlib>

namespace {

/**
 * ホームフォルダ（$HOME）。
 * @return パス（無ければ空）
 */
std::string homeDir() {
    const char* home = std::getenv("HOME");
    return home != nullptr ? home : "";
}

}  // namespace

bool splitCommandLine(const std::string& text, std::vector<std::string>& argv, std::string& error) {
    std::vector<std::string> out;
    std::string current;
    bool inWord = false;      // 引数を読んでいる途中
    bool inQuote = false;     // "…" の中
    bool quotedStart = false; // 今の引数が " で始まった（~/ を広げない）
    for (size_t i = 0; i < text.size(); ++i) {
        const char c = text[i];
        if (inQuote) {
            if (c == '\\' && i + 1 < text.size() && (text[i + 1] == '"' || text[i + 1] == '\\')) {
                current += text[++i];
            } else if (c == '"') {
                inQuote = false;
            } else {
                current += c;
            }
            continue;
        }
        if (c == ' ' || c == '\t' || c == '\n' || c == '\r') {
            if (inWord) {
                if (!quotedStart && current.rfind("~/", 0) == 0) current = homeDir() + current.substr(1);
                out.push_back(current);
                current.clear();
                inWord = false;
            }
            continue;
        }
        if (!inWord) {
            inWord = true;
            quotedStart = c == '"';
        }
        if (c == '"') {
            inQuote = true;
        } else {
            current += c;
        }
    }
    if (inQuote) {
        error = "\" が閉じていません";
        return false;
    }
    if (inWord) {
        if (!quotedStart && current.rfind("~/", 0) == 0) current = homeDir() + current.substr(1);
        out.push_back(current);
    }
    argv = out;
    return true;
}

bool commandDebugEnabled() {
    static const bool enabled = [] {
        const char* value = std::getenv("FRAME_AUX_DEBUG");
        return value != nullptr && std::string(value) == "1";
    }();
    return enabled;
}

std::string commandLogDetail(const std::string& name, const std::string& id, const std::vector<std::string>& argv,
                             bool ok, const std::string& error, bool debug) {
    std::string out = name + "（id " + id + "、引数 " + std::to_string(argv.size()) + " 個）";
    out += ok ? "を起動しました" : "を起動できません: " + error;
    if (debug) out += " ［" + joinCommandLine(argv) + "］";  // 中身（FRAME_AUX_DEBUG=1 のときだけ）
    return out;
}

std::string tryCommandLogLine(const std::string& label, const std::vector<std::string>& argv, bool ok,
                              const std::string& error, bool debug) {
    std::string out = "[試す] " + label + "（引数 " + std::to_string(argv.size()) + " 個）: ";
    out += ok ? "実行しました" : "実行できませんでした: " + error;
    if (debug) out += " ［" + joinCommandLine(argv) + "］";  // 中身（FRAME_AUX_DEBUG=1 のときだけ）
    return out + "\n";
}

std::string joinCommandLine(const std::vector<std::string>& argv) {
    std::string out;
    for (const std::string& arg : argv) {
        if (!out.empty()) out += ' ';
        const bool needQuote = arg.empty() || arg.find_first_of(" \t\n\r\"") != std::string::npos ||
                               arg.rfind("~/", 0) == 0;
        if (!needQuote) {
            out += arg;
            continue;
        }
        out += '"';
        for (const char c : arg) {
            if (c == '"' || c == '\\') out += '\\';
            out += c;
        }
        out += '"';
    }
    return out;
}

std::string commandFolderFor(const std::string& configPath) {
    const size_t slash = configPath.find_last_of('/');
    const std::string dir = slash == std::string::npos ? "." : configPath.substr(0, slash);
    return dir + "/commands";
}

std::vector<CommandDef> scanCommandFolder(const std::string& folder) {
    std::vector<CommandDef> out;
    DIR* dir = ::opendir(folder.c_str());
    if (dir == nullptr) return out;
    while (const dirent* entry = ::readdir(dir)) {
        const std::string file = entry->d_name;
        if (file.empty() || file[0] == '.') continue;
        const std::string path = folder + "/" + file;
        struct stat st {};
        if (::stat(path.c_str(), &st) != 0 || !S_ISREG(st.st_mode) || ::access(path.c_str(), X_OK) != 0) continue;
        CommandDef command;
        command.id = std::string(kFolderCommandPrefix) + file;
        const size_t dot = file.find_last_of('.');
        command.name = dot != std::string::npos && dot > 0 ? file.substr(0, dot) : file;
        command.argv = {path};
        command.fromFolder = true;
        out.push_back(command);
    }
    ::closedir(dir);
    std::sort(out.begin(), out.end(), [](const CommandDef& a, const CommandDef& b) {
        return a.name != b.name ? a.name < b.name : a.id < b.id;
    });
    return out;
}

const CommandDef* findCommand(const std::vector<CommandDef>& commands, const std::vector<CommandDef>& folderCommands,
                              const std::string& id) {
    for (const auto& command : commands) {
        if (command.id == id) return &command;
    }
    for (const auto& command : folderCommands) {
        if (command.id == id) return &command;
    }
    return nullptr;
}

std::string nextCommandId(const std::vector<CommandDef>& commands) {
    for (int n = 1;; ++n) {
        const std::string id = "c" + std::to_string(n);
        const bool used = std::any_of(commands.begin(), commands.end(), [&](const CommandDef& c) { return c.id == id; });
        if (!used) return id;
    }
}
