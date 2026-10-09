// キー対応表の置き換えと戻しの実装。
// 書き込むのは gpio-keys の対応表（EVIOCSKEYCODE_V2）だけ。sysfs・GPIO・デバイスツリーには触らない。
#include "keymap.h"

#include <dirent.h>
#include <fcntl.h>
#include <linux/input.h>
#include <signal.h>
#include <sys/ioctl.h>
#include <unistd.h>

#include <algorithm>
#include <cerrno>
#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>

namespace {

/** 対応表を読むときの index の上限（gpio-keys は 2 項目だが、念のため余裕を持たせる）。 */
constexpr unsigned kMaxKeymapEntries = 64;

/** 置き換えを守っている gpio-keys の fd（シグナルハンドラーから読むので sig_atomic_t）。 */
volatile std::sig_atomic_t gGuardFd = -1;
/** 置き換えたプロセスの PID。fork した子プロセスが落ちても、親の置き換えを戻さないようにする。 */
volatile std::sig_atomic_t gOwnerPid = 0;

/**
 * index を指定して対応表の 1 項目を読む。
 * @param fd 開いたデバイス
 * @param index 読む項目
 * @param entry 書き込み先
 * @return 読めたら true（index が範囲外なら false）
 */
bool getEntry(int fd, unsigned index, input_keymap_entry& entry) {
    std::memset(&entry, 0, sizeof(entry));
    entry.flags = INPUT_KEYMAP_BY_INDEX;
    entry.index = static_cast<__u16>(index);
    return ::ioctl(fd, EVIOCGKEYCODE_V2, &entry) == 0;
}

/**
 * 致命的なシグナルのハンドラー。対応表を戻してから、既定の動き（SA_RESETHAND で戻っている）で同じシグナルを受け直す。
 * ここでは ioctl・getpid・raise だけを使う。
 * @param signal 受けたシグナル
 */
void onFatalSignal(int signal) {
    const int savedErrno = errno;
    if (gGuardFd >= 0 && ::getpid() == gOwnerPid) swapKeycode(gGuardFd, kAuxStandInCode, kAuxOriginalCode);
    // SA_NODEFER なので、ここで送り直したシグナルはすぐ既定の動き（コアダンプ・終了）になる
    ::raise(signal);
    errno = savedErrno;
}

/** atexit から呼ぶ戻し（std::exit で終わった場合の保険）。 */
void restoreAtExit() {
    keymap_guard::restore();
}

}  // namespace

std::string findGpioKeysDevice() {
    DIR* dir = ::opendir("/sys/class/input");
    if (dir == nullptr) return "";
    std::vector<std::string> names;
    while (const dirent* entry = ::readdir(dir)) {
        const std::string name = entry->d_name;
        if (name.rfind("event", 0) == 0) names.push_back(name);
    }
    ::closedir(dir);
    // event3 が event10 より先に来るよう、番号の順に並べる
    std::sort(names.begin(), names.end(), [](const std::string& a, const std::string& b) {
        return std::atoi(a.c_str() + 5) < std::atoi(b.c_str() + 5);
    });
    for (const auto& name : names) {
        std::ifstream file("/sys/class/input/" + name + "/device/name");
        std::string deviceName;
        std::getline(file, deviceName);
        if (deviceName == "gpio-keys") return "/dev/input/" + name;
    }
    return "";
}

std::vector<std::pair<unsigned, unsigned>> readKeymap(int fd) {
    std::vector<std::pair<unsigned, unsigned>> result;
    for (unsigned i = 0; i < kMaxKeymapEntries; ++i) {
        input_keymap_entry entry {};
        if (!getEntry(fd, i, entry)) break;
        result.emplace_back(entry.index, entry.keycode);
    }
    return result;
}

std::string formatKeymap(const std::vector<std::pair<unsigned, unsigned>>& keymap) {
    std::string text = "[";
    for (size_t i = 0; i < keymap.size(); ++i) {
        if (i > 0) text += ", ";
        text += "(" + std::to_string(keymap[i].first) + ", " + std::to_string(keymap[i].second) + ")";
    }
    return text + "]";
}

int swapKeycode(int fd, unsigned from, unsigned to) {
    // シグナルハンドラーからも呼ぶので、ここではメモリを確保しない（スタックの構造体と ioctl だけ）
    int count = 0;
    bool readAny = false;
    for (unsigned i = 0; i < kMaxKeymapEntries; ++i) {
        input_keymap_entry entry {};
        if (!getEntry(fd, i, entry)) break;
        readAny = true;
        if (entry.keycode != from) continue;
        entry.flags = INPUT_KEYMAP_BY_INDEX;
        entry.keycode = to;  // len と scancode は読んだまま渡す
        if (::ioctl(fd, EVIOCSKEYCODE_V2, &entry) != 0) return -1;
        ++count;
    }
    return readAny ? count : -1;
}

namespace keymap_guard {

int attach(const std::string& devicePath, std::string& message) {
    // 読み取りにも使うので非ブロッキング。exec した子プロセスには渡さない
    const int fd = ::open(devicePath.c_str(), O_RDWR | O_NONBLOCK | O_CLOEXEC);
    if (fd < 0) {
        message = devicePath + " を開けません: " + std::strerror(errno);
        return -1;
    }
    const auto before = readKeymap(fd);
    if (before.empty()) {
        message = devicePath + " の対応表を読めません: " + std::strerror(errno);
        ::close(fd);
        return -1;
    }
    const bool hadStandIn = std::any_of(before.begin(), before.end(),
                                        [](const auto& item) { return item.second == kAuxStandInCode; });
    const bool hasOriginal = std::any_of(before.begin(), before.end(),
                                         [](const auto& item) { return item.second == kAuxOriginalCode; });
    if (!hadStandIn && !hasOriginal) {
        message = devicePath + " に aux ボタン（" + std::to_string(kAuxOriginalCode) + "）がありません: " +
                  formatKeymap(before);
        ::close(fd);
        return -1;
    }

    // 置き換える前から守りを有効にしておく（置き換えの途中で落ちても戻せるように）
    gOwnerPid = static_cast<std::sig_atomic_t>(::getpid());
    gGuardFd = fd;
    static bool atexitRegistered = false;
    if (!atexitRegistered) {
        std::atexit(restoreAtExit);
        atexitRegistered = true;
    }
    message = devicePath + " の対応表 " + formatKeymap(before) + (hadStandIn ? "（前回の置き換えが残っています）" : "");
    return fd;
}

bool activate(std::string& message) {
    const int fd = gGuardFd;
    if (fd < 0) {
        message = "gpio-keys を開いていません";
        return false;
    }
    const auto before = readKeymap(fd);
    const int swapped = swapKeycode(fd, kAuxOriginalCode, kAuxStandInCode);
    if (swapped < 0) {
        message = "対応表を書き換えられません: " + std::string(std::strerror(errno));
        restore();
        return false;
    }
    message = formatKeymap(before) + " → " + formatKeymap(readKeymap(fd)) +
              (swapped == 0 ? "（置き換え済みでした）" : "");
    return true;
}

bool deactivate(std::string& message) {
    const int fd = gGuardFd;
    if (fd < 0) {
        message = "gpio-keys を開いていません";
        return false;
    }
    const auto before = readKeymap(fd);
    const int restored = restore();
    if (restored < 0) {
        message = "対応表を戻せません: " + std::string(std::strerror(errno));
        return false;
    }
    message = formatKeymap(before) + " → " + formatKeymap(readKeymap(fd)) +
              (restored == 0 ? "（置き換えていませんでした）" : "");
    return true;
}

int remap(const std::string& devicePath, std::string& message) {
    std::string attachMessage;
    const int fd = attach(devicePath, attachMessage);
    if (fd < 0) {
        message = attachMessage;
        return -1;
    }
    std::string activateMessage;
    if (!activate(activateMessage)) {
        message = devicePath + ": " + activateMessage;
        gGuardFd = -1;
        ::close(fd);
        return -1;
    }
    message = devicePath + " の対応表 " + activateMessage;
    return fd;
}

int restore() {
    const int fd = gGuardFd;
    if (fd < 0) return 0;
    if (::getpid() != gOwnerPid) return 0;
    return swapKeycode(fd, kAuxStandInCode, kAuxOriginalCode);
}

void installCrashHandlers() {
    // 既定の動きが「終了」か「コアダンプ」のシグナルのうち、行儀よく終わる経路（SIGTERM・SIGINT・SIGHUP・SIGUSR2）以外。
    // SIGKILL・SIGSTOP は受けられないので、そのときは戻らない（再起動か --restore で戻す）
    const int signals[] = {SIGSEGV, SIGABRT, SIGBUS,  SIGFPE,    SIGILL,  SIGTRAP, SIGSYS,  SIGQUIT,
                           SIGXCPU, SIGXFSZ, SIGALRM, SIGVTALRM, SIGPROF, SIGUSR1};
    for (const int signal : signals) {
        struct sigaction action {};
        action.sa_handler = onFatalSignal;
        // ハンドラーの間は他のシグナルで割り込まれないようにする。受けたシグナル自身だけは止めない
        // （ハンドラーの最後に送り直したとき、すぐ既定の動きになるように）
        sigfillset(&action.sa_mask);
        sigdelset(&action.sa_mask, signal);
        action.sa_flags = SA_RESETHAND | SA_NODEFER;
        ::sigaction(signal, &action, nullptr);
    }
}

}  // namespace keymap_guard
