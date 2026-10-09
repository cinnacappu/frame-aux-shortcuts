// 実行時のファイルの置き場所と、ほかのプロセスの確かめ方の実装。
#include "runtime.h"

#include "json.h"

#include <fcntl.h>
#include <signal.h>
#include <sys/stat.h>
#include <unistd.h>

#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <sstream>

std::string runtimeDir() {
    const char* runtime = std::getenv("XDG_RUNTIME_DIR");
    if (runtime != nullptr && runtime[0] != '\0') return runtime;
    const std::string userRuntime = "/run/user/" + std::to_string(::getuid());
    if (::access(userRuntime.c_str(), W_OK) == 0) return userRuntime;
    return "/tmp";
}

std::string appRuntimeDir() {
    const std::string dir = runtimeDir();
    if (dir == "/tmp") return "/tmp/frame-aux-shortcuts-" + std::to_string(::getuid());
    return dir + "/frame-aux-shortcuts";
}

std::string lockFilePath(const std::string& app) {
    const std::string dir = runtimeDir();
    if (dir != "/tmp") return dir + "/" + app + ".lock";
    // /tmp の予備: このアプリのロックは自分だけのフォルダの中。ほかのアプリ（読むだけ）はそのアプリの決め方
    if (app == kDaemonName || app == kPanelName) return appRuntimeDir() + "/" + app + ".lock";
    return "/tmp/" + app + "-" + std::to_string(::getuid()) + ".lock";
}

std::string statusFilePath() {
    return appRuntimeDir() + "/status.json";
}

bool checkPrivateDir(const std::string& dir, uid_t uid, std::string& reason) {
    struct stat st {};
    if (::lstat(dir.c_str(), &st) != 0) {
        reason = dir + " を確かめられません: " + std::strerror(errno);
        return false;
    }
    if (S_ISLNK(st.st_mode)) {
        reason = dir + " はシンボリックリンクなので使いません";
        return false;
    }
    if (!S_ISDIR(st.st_mode)) {
        reason = dir + " はフォルダではないので使いません";
        return false;
    }
    if (st.st_uid != uid) {
        reason = dir + " の持ち主がほかのユーザー（uid " + std::to_string(st.st_uid) + "）なので使いません";
        return false;
    }
    if ((st.st_mode & 077) != 0) {
        char mode[8];
        std::snprintf(mode, sizeof(mode), "%03o", static_cast<unsigned>(st.st_mode & 0777));
        reason = dir + " はほかのユーザーにも権限がある（" + mode + "）ので使いません";
        return false;
    }
    return true;
}

bool ensurePrivateDir(const std::string& dir, std::string& reason) {
    if (::mkdir(dir.c_str(), 0700) != 0 && errno != EEXIST) {
        reason = dir + " を作れません: " + std::strerror(errno);
        return false;
    }
    return checkPrivateDir(dir, ::getuid(), reason);
}

int openPrivateFile(const std::string& path, std::string& reason) {
    const int fd = ::open(path.c_str(), O_RDWR | O_CREAT | O_NOFOLLOW | O_CLOEXEC, 0600);
    if (fd < 0) {
        reason = path + " を開けません: " + std::strerror(errno);
        return -1;
    }
    struct stat st {};
    if (::fstat(fd, &st) != 0 || !S_ISREG(st.st_mode) || st.st_uid != ::getuid()) {
        reason = path + " はふつうのファイルでないか、持ち主が自分でないので使いません";
        ::close(fd);
        return -1;
    }
    return fd;
}

int openLockFile(const std::string& app, std::string& reason) {
    if (runtimeDir() == "/tmp" && !ensurePrivateDir(appRuntimeDir(), reason)) return -1;
    return openPrivateFile(lockFilePath(app), reason);
}

bool readTextFile(const std::string& path, std::string& text) {
    std::ifstream file(path, std::ios::binary);
    if (!file) return false;
    std::stringstream buffer;
    buffer << file.rdbuf();
    text = buffer.str();
    return true;
}

bool isCmdlineOf(const std::string& cmdline, const std::string& name) {
    const std::string argv0 = cmdline.substr(0, cmdline.find('\0'));
    const size_t slash = argv0.rfind('/');
    const std::string base = slash == std::string::npos ? argv0 : argv0.substr(slash + 1);
    return !base.empty() && base == name;
}

bool processIs(pid_t pid, const std::string& name) {
    if (pid <= 1) return false;
    if (::kill(pid, 0) != 0 && errno != EPERM) return false;
    const std::string proc = "/proc/" + std::to_string(pid);
    std::string cmdline;
    if (readTextFile(proc + "/cmdline", cmdline) && !cmdline.empty()) return isCmdlineOf(cmdline, name);
    // cmdline が読めないとき（ゾンビなど）は comm で見る（15 文字で切れる）
    std::string comm;
    if (!readTextFile(proc + "/comm", comm)) return false;
    while (!comm.empty() && comm.back() == '\n') comm.pop_back();
    return comm == name.substr(0, 15);
}

pid_t runningPid(const std::string& app) {
    std::string text;
    if (!readTextFile(lockFilePath(app), text)) return 0;
    const long pid = std::atol(text.c_str());
    if (pid <= 1) return 0;
    return processIs(static_cast<pid_t>(pid), app) ? static_cast<pid_t>(pid) : 0;
}

bool isInstalled(const std::string& name) {
    const char* home = std::getenv("HOME");
    if (home != nullptr && ::access((std::string(home) + "/.local/bin/" + name).c_str(), X_OK) == 0) return true;
    const char* path = std::getenv("PATH");
    if (path == nullptr) return false;
    std::stringstream dirs(path);
    std::string dir;
    while (std::getline(dirs, dir, ':')) {
        if (!dir.empty() && ::access((dir + "/" + name).c_str(), X_OK) == 0) return true;
    }
    return false;
}

bool runningAsUnit(const std::string& unit) {
    std::string text;
    if (!readTextFile("/proc/self/cgroup", text)) return false;
    return text.find("/" + unit) != std::string::npos;
}

bool writeFileAtomically(const std::string& path, const std::string& text, std::string& error) {
    const size_t slash = path.find_last_of('/');
    if (slash != std::string::npos && slash > 0 && !ensurePrivateDir(path.substr(0, slash), error)) return false;
    return writeFilePrivately(path, text, error);
}

bool writeFilePrivately(const std::string& path, const std::string& text, std::string& error) {
    // 一時ファイルは毎回新しく作る（O_EXCL。前に落ちて残った一時ファイルや、置かれたリンクには書かない）
    std::string temp;
    int fd = -1;
    for (int attempt = 0; attempt < 16 && fd < 0; ++attempt) {
        temp = path + ".tmp." + std::to_string(::getpid()) + "." + std::to_string(attempt);
        fd = ::open(temp.c_str(), O_WRONLY | O_CREAT | O_EXCL | O_NOFOLLOW | O_CLOEXEC, 0600);
        if (fd < 0 && errno != EEXIST) break;
    }
    if (fd < 0) {
        error = "書き込めません: " + temp + "（" + std::strerror(errno) + "）";
        return false;
    }
    size_t written = 0;
    while (written < text.size()) {
        const ssize_t n = ::write(fd, text.data() + written, text.size() - written);
        if (n < 0 && errno == EINTR) continue;
        if (n <= 0) break;
        written += static_cast<size_t>(n);
    }
    const bool ok = written == text.size() && ::fsync(fd) == 0;
    ::close(fd);
    if (!ok) {
        error = "書き込みに失敗しました: " + temp;
        ::unlink(temp.c_str());
        return false;
    }
    if (std::rename(temp.c_str(), path.c_str()) != 0) {
        error = "置き換えに失敗しました: " + std::string(std::strerror(errno));
        ::unlink(temp.c_str());
        return false;
    }
    return true;
}

bool parseWpctlMuted(const std::string& text, bool& muted) {
    if (text.find("Volume:") == std::string::npos) return false;
    muted = text.find("[MUTED]") != std::string::npos;
    return true;
}

int readPerfOverlayVisible() {
    const char* xdg = std::getenv("XDG_CONFIG_HOME");
    const char* home = std::getenv("HOME");
    const std::string base = xdg != nullptr && xdg[0] != '\0' ? std::string(xdg)
                                                             : std::string(home != nullptr ? home : ".") + "/.config";
    std::string text;
    if (!readTextFile(base + "/frame-perf-overlay/config.json", text)) return -1;
    JsonValue root;
    std::string error;
    if (!parseJson(text, root, error)) return -1;
    const JsonValue* visible = root.get("visible");
    if (visible == nullptr || !visible->isBool()) return -1;
    return visible->boolean ? 1 : 0;
}
