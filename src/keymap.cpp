// キー対応表の置き換えと戻しの実装。
// 書き込むのは gpio-keys の対応表（EVIOCSKEYCODE_V2）と、aux の元の動きの入力（evdev への input_event の write）だけ。
// sysfs・GPIO・デバイスツリーには触らない。
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

/** 元の aux の押して離すの間（実機で 0.08 秒で SteamVR に届いた）。 */
constexpr useconds_t kOriginalPressHoldUs = 80 * 1000;
/** 置き直しに失敗したときに試し直す回数。 */
constexpr int kRemapRetries = 3;

/** 置き換えを守っている gpio-keys の fd（シグナルハンドラーから読むので sig_atomic_t）。 */
volatile std::sig_atomic_t gGuardFd = -1;
/** 置き換えたプロセスの PID。fork した子プロセスが落ちても、親の置き換えを戻さないようにする。 */
volatile std::sig_atomic_t gOwnerPid = 0;
/** pressOriginal() で置き直せず、353 のまま残ったかもしれない（takeRemapLost() で一度だけ返す）。メインスレッドだけで使う。 */
bool gRemapLost = false;

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

/**
 * キーの押し・離しを 1 つ（EV_KEY と SYN_REPORT）デバイスに書く（evdev への書き込みは、ほかの読み手への入力になる）。
 * 時刻はカーネルが付けるので 0 のまま。
 * @param fd 開いたデバイス（書ける）
 * @param code キーコード
 * @param value 1 なら押す、0 なら離す
 * @return 書けたら true
 */
bool writeKey(int fd, unsigned code, int value) {
    input_event events[2] {};
    events[0].type = EV_KEY;
    events[0].code = static_cast<__u16>(code);
    events[0].value = value;
    events[1].type = EV_SYN;
    events[1].code = SYN_REPORT;
    events[1].value = 0;
    const ssize_t written = ::write(fd, events, sizeof(events));
    return written == static_cast<ssize_t>(sizeof(events));
}

/**
 * KEY_SELECT を押して離す（間は kOriginalPressHoldUs）。押したのに離せなかったときも、離すのを 1 回試し直す。
 * @param fd 開いたデバイス（書ける）
 * @param error 書けなかったときの理由
 * @return 押して離すを書けたら true
 */
bool writeOriginalPress(int fd, std::string& error) {
    if (!writeKey(fd, kAuxOriginalCode, 1)) {
        error = std::string("押すのを書けません: ") + std::strerror(errno);
        return false;
    }
    ::usleep(kOriginalPressHoldUs);
    if (writeKey(fd, kAuxOriginalCode, 0)) return true;
    error = std::string("離すのを書けません: ") + std::strerror(errno);
    writeKey(fd, kAuxOriginalCode, 0);
    return false;
}

/**
 * 353 → 656 に置き直す（失敗したら少し待って試し直す）。
 * @param fd 開いたデバイス
 * @return 置き直せたら true
 */
bool remapAgain(int fd) {
    for (int i = 0; i < kRemapRetries; ++i) {
        if (swapKeycode(fd, kAuxOriginalCode, kAuxStandInCode) >= 0) return true;
        ::usleep(10 * 1000);
    }
    return false;
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

bool pressOriginal(std::string& message) {
    // 置き換えの持ち主（attach したプロセス）なら守っている fd を使う。そうでなければ（--test-action）自分で開く
    const bool owner = gGuardFd >= 0 && ::getpid() == gOwnerPid;
    int fd = owner ? static_cast<int>(gGuardFd) : -1;
    if (!owner) {
        const std::string device = findGpioKeysDevice();
        if (device.empty()) {
            message = "gpio-keys が見つかりません";
            return false;
        }
        fd = ::open(device.c_str(), O_RDWR | O_CLOEXEC);
        if (fd < 0) {
            message = device + " を開けません: " + std::strerror(errno);
            return false;
        }
    }
    const auto keymap = readKeymap(fd);
    const bool remapped = std::any_of(keymap.begin(), keymap.end(),
                                      [](const auto& item) { return item.second == kAuxStandInCode; });
    const bool hasOriginal = std::any_of(keymap.begin(), keymap.end(),
                                         [](const auto& item) { return item.second == kAuxOriginalCode; });
    bool ok = false;
    std::string error;
    if (!remapped) {
        if (!hasOriginal) {
            message = "対応表に aux（" + std::to_string(kAuxOriginalCode) + "）がありません: " + formatKeymap(keymap);
        } else {
            ok = writeOriginalPress(fd, error);
            message = ok ? "置き換えていないので、そのまま KEY_SELECT を押して離しました" : error;
        }
    } else if (!owner) {
        message = "本体が aux を置き換えているので書きません（本体の押し方に割り当てて試してください）";
    } else if (swapKeycode(fd, kAuxStandInCode, kAuxOriginalCode) < 0) {
        message = "対応表を一時的に戻せません: " + std::string(std::strerror(errno));
        if (!remapAgain(fd)) gRemapLost = true;  // 途中まで戻っていても、置き換えに揃える
    } else {
        // ここから置き直すまでの間（約 80 ms）は、本物の aux も SteamVR に届く
        const bool wrote = writeOriginalPress(fd, error);
        const bool back = remapAgain(fd);
        ok = wrote && back;
        if (!back) {
            const int err = errno;  // readKeymap の ioctl で上書きされる前に取る
            gRemapLost = true;
            message = "置き直せません（aux が SteamVR に届くままです）: " + std::string(std::strerror(err)) + " " +
                      formatKeymap(readKeymap(fd));
        } else {
            message = wrote ? "一時的に 353 に戻して KEY_SELECT を押して離し、656 に置き直しました"
                            : error + "（656 には置き直しました）";
        }
    }
    if (!owner) ::close(fd);
    return ok;
}

bool takeRemapLost() {
    const bool lost = gRemapLost;
    gRemapLost = false;
    return lost;
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
