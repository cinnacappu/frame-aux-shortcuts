// つないでいる PC に OSC を送る組み込みのコマンドの実装。
#include "osc.h"

#include "runtime.h"

#include <arpa/inet.h>
#include <dirent.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

#include <algorithm>
#include <cerrno>
#include <chrono>
#include <cstdlib>
#include <cstring>
#include <sstream>
#include <thread>

namespace {

/**
 * OSC の文字列（NUL で終わり、4 バイトの境目まで 0 で埋める）を足す。
 * @param out 追加先
 * @param text 文字列
 */
void appendOscString(std::string& out, const std::string& text) {
    out += text;
    const size_t pad = 4 - text.size() % 4;  // NUL を必ず 1 つ以上
    out.append(pad, '\0');
}

/**
 * 10 進の数の並びを読む（符号・空白・先頭の 0 は認めない。"0" だけはよい）。
 * @param text 文字列
 * @param max 上限
 * @param value 読めたときの書き込み先
 * @return 読めたら true
 */
bool parseDecimal(const std::string& text, long max, long& value) {
    if (text.empty() || text.size() > 5 || (text.size() > 1 && text[0] == '0')) return false;
    long v = 0;
    for (const char c : text) {
        if (c < '0' || c > '9') return false;
        v = v * 10 + (c - '0');
    }
    if (v > max) return false;
    value = v;
    return true;
}

/**
 * /proc/net の 16 進のアドレス（32 ビットごとにそのマシンのバイト順）を IP の文字列にする。
 * @param hex 8 文字（IPv4）か 32 文字（IPv6）
 * @param ip 書き込み先
 * @param loopbackOrZero ループバックか 0（どこでもない）なら true
 * @return 読めたら true
 */
bool procAddressToIp(const std::string& hex, std::string& ip, bool& loopbackOrZero) {
    if (hex.size() != 8 && hex.size() != 32) return false;
    unsigned char bytes[16] = {};
    for (size_t word = 0; word < hex.size() / 8; ++word) {
        const std::string part = hex.substr(word * 8, 8);
        char* end = nullptr;
        const unsigned long value = std::strtoul(part.c_str(), &end, 16);
        if (end != part.c_str() + 8) return false;
        const uint32_t native = static_cast<uint32_t>(value);
        std::memcpy(bytes + word * 4, &native, 4);  // カーネルはメモリの 4 バイトをそのまま数として書いている
    }
    char text[INET6_ADDRSTRLEN] = {};
    if (hex.size() == 8) {
        loopbackOrZero = bytes[0] == 127 || (bytes[0] | bytes[1] | bytes[2] | bytes[3]) == 0;
        return inet_ntop(AF_INET, bytes, text, sizeof(text)) != nullptr && (ip = text, true);
    }
    static const unsigned char kMapped[12] = {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0xff, 0xff};
    if (std::memcmp(bytes, kMapped, 12) == 0) {
        // IPv4 を写した IPv6（::ffff:a.b.c.d）は IPv4 として扱う
        loopbackOrZero = bytes[12] == 127 || (bytes[12] | bytes[13] | bytes[14] | bytes[15]) == 0;
        return inet_ntop(AF_INET, bytes + 12, text, sizeof(text)) != nullptr && (ip = text, true);
    }
    static const unsigned char kZero[16] = {};
    static const unsigned char kLoopback[16] = {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1};
    loopbackOrZero = std::memcmp(bytes, kZero, 16) == 0 || std::memcmp(bytes, kLoopback, 16) == 0;
    return inet_ntop(AF_INET6, bytes, text, sizeof(text)) != nullptr && (ip = text, true);
}

/**
 * Steam Link の受け手（実行ファイル名 vrlink）のプロセスが持つソケットの inode を集める。
 * 主スレッドが名前を変える（comm は "vrlinkrunthread"）ので、/proc/<pid>/exe のファイル名で見分ける。
 * @return inode の一覧（見つからなければ空）
 */
std::vector<unsigned long long> steamLinkSocketInodes() {
    std::vector<unsigned long long> inodes;
    DIR* proc = ::opendir("/proc");
    if (proc == nullptr) return inodes;
    while (const dirent* entry = ::readdir(proc)) {
        const char* name = entry->d_name;
        if (name[0] < '1' || name[0] > '9') continue;
        const std::string base = std::string("/proc/") + name;
        char exe[4096] = {};
        const ssize_t n = ::readlink((base + "/exe").c_str(), exe, sizeof(exe) - 1);
        if (n <= 0) continue;
        std::string path(exe, static_cast<size_t>(n));
        const std::string deleted = " (deleted)";
        if (path.size() > deleted.size() && path.compare(path.size() - deleted.size(), deleted.size(), deleted) == 0) {
            path.erase(path.size() - deleted.size());
        }
        if (path.substr(path.find_last_of('/') + 1) != "vrlink") continue;
        DIR* fds = ::opendir((base + "/fd").c_str());
        if (fds == nullptr) continue;
        while (const dirent* fd = ::readdir(fds)) {
            if (fd->d_name[0] == '.') continue;
            char link[256] = {};
            const ssize_t m = ::readlink((base + "/fd/" + fd->d_name).c_str(), link, sizeof(link) - 1);
            if (m <= 0) continue;
            const std::string target(link, static_cast<size_t>(m));
            const std::string prefix = "socket:[";
            if (target.compare(0, prefix.size(), prefix) != 0 || target.back() != ']') continue;
            inodes.push_back(std::strtoull(target.c_str() + prefix.size(), nullptr, 10));
        }
        ::closedir(fds);
    }
    ::closedir(proc);
    return inodes;
}

}  // namespace

std::string encodeOscInt(const std::string& address, int32_t value) {
    std::string out;
    appendOscString(out, address);
    appendOscString(out, ",i");
    const uint32_t v = static_cast<uint32_t>(value);
    for (int shift = 24; shift >= 0; shift -= 8) out += static_cast<char>((v >> shift) & 0xFF);
    return out;
}

bool isValidIpv4(const std::string& text) {
    std::stringstream parts(text);
    std::string part;
    int count = 0;
    while (std::getline(parts, part, '.')) {
        long value = 0;
        if (!parseDecimal(part, 255, value) || part.size() > 3) return false;
        ++count;
    }
    return count == 4 && !text.empty() && text.back() != '.';
}

bool parseOscTarget(const std::string& spec, SendOscCommand& out) {
    const size_t colon = spec.rfind(':');
    const std::string host = colon == std::string::npos ? spec : spec.substr(0, colon);
    long port = kVrchatOscPort;
    if (colon != std::string::npos && (!parseDecimal(spec.substr(colon + 1), 65535, port) || port == 0)) return false;
    if (host == kOscAutoHost) {
        out.autoHost = true;
        out.host.clear();
    } else if (isValidIpv4(host)) {
        out.autoHost = false;
        out.host = host;
    } else {
        return false;
    }
    out.port = static_cast<int>(port);
    return true;
}

bool parseSendOscCommand(const std::vector<std::string>& argv, SendOscCommand& out) {
    if (argv.size() != 4 || argv[1] != kSendOscFlag) return false;
    const std::string& program = argv[0];
    if (program.substr(program.find_last_of('/') + 1) != kSendOscProgram) return false;
    const std::string& address = argv[3];
    if (address.size() < 2 || address.size() > kMaxOscAddress || address[0] != '/' ||
        address.find_first_of(" \t#*,?[]{}") != std::string::npos) {
        return false;
    }
    for (const char c : address) {
        const unsigned char u = static_cast<unsigned char>(c);
        if (u < 0x20 || u == 0x7F) return false;  // 制御文字・NUL（OSC の文字列の終わりと混ざる）
    }
    SendOscCommand command;
    if (!parseOscTarget(argv[2], command)) return false;
    command.address = address;
    out = command;
    return true;
}

std::vector<std::string> makeSendOscArgv(bool autoHost, const std::string& host, int port, const std::string& address) {
    return {kSendOscProgram, kSendOscFlag, (autoHost ? std::string(kOscAutoHost) : host) + ":" + std::to_string(port),
            address};
}

bool isLanAddress(const std::string& ip) {
    unsigned char b[16] = {};
    if (inet_pton(AF_INET, ip.c_str(), b) == 1) {
        return b[0] == 10 || (b[0] == 172 && (b[1] & 0xF0) == 16) || (b[0] == 192 && b[1] == 168) ||
               (b[0] == 169 && b[1] == 254) || (b[0] == 100 && (b[1] & 0xC0) == 64);
    }
    if (inet_pton(AF_INET6, ip.c_str(), b) != 1) return false;
    static const unsigned char kMapped[12] = {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0xff, 0xff};
    if (std::memcmp(b, kMapped, 12) == 0) {
        char v4[INET_ADDRSTRLEN] = {};
        return inet_ntop(AF_INET, b + 12, v4, sizeof(v4)) != nullptr && isLanAddress(v4);
    }
    return (b[0] & 0xFE) == 0xFC || (b[0] == 0xFE && (b[1] & 0xC0) == 0x80);
}

bool findConnectedUdpPeer(const std::string& table, const std::vector<unsigned long long>& inodes, std::string& ip) {
    // 候補は私的な範囲のアドレスだけ。違うアドレスが 2 つ以上あれば、どれが PC か分からないので「見つからない」
    std::vector<std::string> found;
    std::stringstream lines(table);
    std::string line;
    std::getline(lines, line);  // 見出し
    while (std::getline(lines, line)) {
        // "  sl  local_address rem_address   st tx_queue rx_queue tr tm->when retrnsmt   uid  timeout inode ..."
        std::stringstream fields(line);
        std::vector<std::string> f;
        std::string field;
        while (fields >> field && f.size() < 10) f.push_back(field);
        if (f.size() < 10 || f[3] != "01") continue;
        if (!inodes.empty()) {
            const unsigned long long inode = std::strtoull(f[9].c_str(), nullptr, 10);
            if (std::find(inodes.begin(), inodes.end(), inode) == inodes.end()) continue;
        }
        const std::string& remote = f[2];
        const size_t colon = remote.find(':');
        if (colon == std::string::npos) continue;
        std::string text;
        bool skip = false;
        if (!procAddressToIp(remote.substr(0, colon), text, skip) || skip || !isLanAddress(text)) continue;
        if (std::find(found.begin(), found.end(), text) == found.end()) found.push_back(text);
    }
    if (found.size() != 1) return false;
    ip = found[0];
    return true;
}

bool findSteamLinkPeer(std::string& ip) {
    const std::vector<unsigned long long> inodes = steamLinkSocketInodes();
    if (inodes.empty()) return false;
    // IPv4 と IPv6 の表をまとめて見る（両方に別の相手があれば、どちらかに決めない）
    std::string both;
    for (const char* path : {"/proc/net/udp", "/proc/net/udp6"}) {
        std::string table;
        if (!readTextFile(path, table)) continue;
        if (!both.empty()) table.erase(0, table.find('\n') + 1);  // 2 つ目の見出しを落とす
        both += table;
        if (!both.empty() && both.back() != '\n') both += '\n';
    }
    return findConnectedUdpPeer(both, inodes, ip);
}

SendOscResult sendOscPress(const SendOscCommand& command, int holdMs) {
    SendOscResult result;
    std::string host = command.host;
    if (command.autoHost && !findSteamLinkPeer(host)) {
        result.reason = "no_pc";
        result.error = "つないでいる PC が見つかりません（Steam Link でつないでいないかも）";
        return result;
    }
    result.host = host;
    sockaddr_storage storage {};
    socklen_t length = 0;
    int family = AF_INET;
    if (host.find(':') == std::string::npos) {
        sockaddr_in* in = reinterpret_cast<sockaddr_in*>(&storage);
        in->sin_family = AF_INET;
        in->sin_port = htons(static_cast<uint16_t>(command.port));
        if (inet_pton(AF_INET, host.c_str(), &in->sin_addr) != 1) host.clear();
        length = sizeof(sockaddr_in);
    } else {
        family = AF_INET6;
        sockaddr_in6* in6 = reinterpret_cast<sockaddr_in6*>(&storage);
        in6->sin6_family = AF_INET6;
        in6->sin6_port = htons(static_cast<uint16_t>(command.port));
        if (inet_pton(AF_INET6, host.c_str(), &in6->sin6_addr) != 1) host.clear();
        length = sizeof(sockaddr_in6);
    }
    if (host.empty()) {
        result.reason = "failed";
        result.error = "送り先のアドレスを読めません";
        return result;
    }
    const int fd = ::socket(family, SOCK_DGRAM | SOCK_CLOEXEC, 0);
    if (fd < 0) {
        result.reason = "failed";
        result.error = std::string("ソケットを作れません: ") + std::strerror(errno);
        return result;
    }
    const auto send = [&](int32_t value) {
        const std::string packet = encodeOscInt(command.address, value);
        return ::sendto(fd, packet.data(), packet.size(), 0, reinterpret_cast<const sockaddr*>(&storage), length) ==
               static_cast<ssize_t>(packet.size());
    };
    bool ok = send(1);
    if (ok) {
        std::this_thread::sleep_for(std::chrono::milliseconds(std::max(0, holdMs)));
        // 0 も送る（送らないと、VRChat ではコントローラー・キーボードでミュートを切り替えられなくなる）
        ok = send(0);
        if (!ok) {
            std::this_thread::sleep_for(std::chrono::milliseconds(50));
            ok = send(0);
        }
    }
    if (!ok) {
        result.reason = "failed";
        result.error = std::string("送れません: ") + std::strerror(errno);
    }
    ::close(fd);
    result.ok = ok;
    return result;
}
