// room view の切り替えの実装。小さな WebSocket クライアント（ハンドシェイクとマスク付きテキストフレームの送信だけ）。
// 非公開の仕組みを使っている（room_view.h の注意を参照）。
#include "room_view.h"

#include <arpa/inet.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <poll.h>
#include <sys/socket.h>
#include <unistd.h>

#include <cerrno>
#include <cstring>
#include <random>

namespace {

constexpr const char* kHost = "127.0.0.1";
constexpr uint16_t kPort = 27062;
constexpr int kTimeoutMs = 1000;  ///< 接続・送信・応答の読み取りの上限（ms）

/**
 * 乱数のバイト列を作る（WebSocket のキーとマスク用。暗号の強さは要らない）。
 * @param out 書き込み先
 * @param size バイト数
 */
void randomBytes(uint8_t* out, size_t size) {
    std::random_device device;
    for (size_t i = 0; i < size; ++i) out[i] = static_cast<uint8_t>(device() & 0xff);
}

/**
 * Base64 にする（Sec-WebSocket-Key 用）。
 * @param data バイト列
 * @param size バイト数
 * @return Base64 の文字列
 */
std::string base64(const uint8_t* data, size_t size) {
    static const char kTable[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    std::string out;
    for (size_t i = 0; i < size; i += 3) {
        const uint32_t n = (static_cast<uint32_t>(data[i]) << 16) |
                           (i + 1 < size ? static_cast<uint32_t>(data[i + 1]) << 8 : 0) |
                           (i + 2 < size ? static_cast<uint32_t>(data[i + 2]) : 0);
        out += kTable[(n >> 18) & 63];
        out += kTable[(n >> 12) & 63];
        out += i + 1 < size ? kTable[(n >> 6) & 63] : '=';
        out += i + 2 < size ? kTable[n & 63] : '=';
    }
    return out;
}

/**
 * 時間切れつきで全部送る。
 * @param fd ソケット（非ブロッキング）
 * @param data 送るバイト列
 * @param error 失敗したときの理由
 * @return 全部送れたら true
 */
bool sendAll(int fd, const std::string& data, std::string& error) {
    size_t sent = 0;
    while (sent < data.size()) {
        const ssize_t n = ::send(fd, data.data() + sent, data.size() - sent, MSG_NOSIGNAL);
        if (n > 0) {
            sent += static_cast<size_t>(n);
            continue;
        }
        if (n < 0 && errno == EINTR) continue;
        if (n < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) {
            pollfd pfd {fd, POLLOUT, 0};
            if (::poll(&pfd, 1, kTimeoutMs) > 0) continue;
            error = "送信が時間切れです";
            return false;
        }
        error = std::string("send: ") + std::strerror(errno);
        return false;
    }
    return true;
}

/**
 * HTTP の応答ヘッダーを最後（空行）まで読む。
 * @param fd ソケット（非ブロッキング）
 * @param header 読んだヘッダーの書き込み先
 * @param error 失敗したときの理由
 * @return 読めたら true
 */
bool readHandshakeResponse(int fd, std::string& header, std::string& error) {
    char buffer[1024];
    while (header.find("\r\n\r\n") == std::string::npos) {
        if (header.size() > 8192) {
            error = "応答のヘッダーが長すぎます";
            return false;
        }
        pollfd pfd {fd, POLLIN, 0};
        const int ready = ::poll(&pfd, 1, kTimeoutMs);
        if (ready < 0 && errno == EINTR) continue;
        if (ready <= 0) {
            error = "応答が時間切れです";
            return false;
        }
        const ssize_t n = ::recv(fd, buffer, sizeof(buffer), 0);
        if (n > 0) {
            header.append(buffer, static_cast<size_t>(n));
            continue;
        }
        if (n < 0 && (errno == EINTR || errno == EAGAIN)) continue;
        error = n == 0 ? "応答の途中で切られました" : std::string("recv: ") + std::strerror(errno);
        return false;
    }
    return true;
}

/**
 * 時間切れつきで 127.0.0.1:27062 につなぐ。
 * @param error 失敗したときの理由
 * @return つながったソケット（非ブロッキング）。失敗なら -1
 */
int connectWithTimeout(std::string& error) {
    const int fd = ::socket(AF_INET, SOCK_STREAM | SOCK_NONBLOCK | SOCK_CLOEXEC, 0);
    if (fd < 0) {
        error = std::string("socket: ") + std::strerror(errno);
        return -1;
    }
    sockaddr_in address {};
    address.sin_family = AF_INET;
    address.sin_port = htons(kPort);
    ::inet_pton(AF_INET, kHost, &address.sin_addr);
    if (::connect(fd, reinterpret_cast<sockaddr*>(&address), sizeof(address)) != 0) {
        if (errno != EINPROGRESS) {
            error = std::string("connect: ") + std::strerror(errno);
            ::close(fd);
            return -1;
        }
        pollfd pfd {fd, POLLOUT, 0};
        int ready = 0;
        do {
            ready = ::poll(&pfd, 1, kTimeoutMs);
        } while (ready < 0 && errno == EINTR);
        int socketError = 0;
        socklen_t length = sizeof(socketError);
        if (ready <= 0 || ::getsockopt(fd, SOL_SOCKET, SO_ERROR, &socketError, &length) != 0 || socketError != 0) {
            error = ready <= 0 ? "接続が時間切れです" : std::string("connect: ") + std::strerror(socketError);
            ::close(fd);
            return -1;
        }
    }
    return fd;
}

}  // namespace

std::string roomViewMailboxMessage(bool enabled) {
    return std::string("mailbox_send vrcompositor_systemlayer {\"type\":\"set_camera_roomview_enabled\",\"enabled\":") +
           (enabled ? "true" : "false") + "}";
}

std::string buildMaskedTextFrame(const std::string& text, const uint8_t mask[4]) {
    if (text.size() > 0xffff) return "";
    std::string frame;
    frame += static_cast<char>(0x81);  // FIN + テキスト
    if (text.size() < 126) {
        frame += static_cast<char>(0x80 | text.size());
    } else {
        frame += static_cast<char>(0x80 | 126);
        frame += static_cast<char>((text.size() >> 8) & 0xff);
        frame += static_cast<char>(text.size() & 0xff);
    }
    for (int i = 0; i < 4; ++i) frame += static_cast<char>(mask[i]);
    for (size_t i = 0; i < text.size(); ++i) frame += static_cast<char>(static_cast<uint8_t>(text[i]) ^ mask[i % 4]);
    return frame;
}

bool sendRoomViewEnabled(bool enabled, std::string& error) {
    const int fd = connectWithTimeout(error);
    if (fd < 0) return false;

    uint8_t keyBytes[16];
    randomBytes(keyBytes, sizeof(keyBytes));
    const std::string request = std::string("GET / HTTP/1.1\r\n") + "Host: 127.0.0.1:27062\r\n" +
                                "Upgrade: websocket\r\n" + "Connection: Upgrade\r\n" +
                                "Sec-WebSocket-Key: " + base64(keyBytes, sizeof(keyBytes)) + "\r\n" +
                                "Sec-WebSocket-Version: 13\r\n" + "Origin: http://127.0.0.1:27062\r\n\r\n";
    std::string header;
    bool ok = sendAll(fd, request, error) && readHandshakeResponse(fd, header, error);
    if (ok && header.compare(0, 12, "HTTP/1.1 101") != 0) {
        error = "WebSocket に切り替えられません: " + header.substr(0, header.find("\r\n"));
        ok = false;
    }
    if (ok) {
        // メールボックスの名前は、ほかのクライアントとぶつからないよう PID を入れる
        const std::string messages[] = {
            "mailbox_open frame_aux_shortcuts/" + std::to_string(::getpid()),
            roomViewMailboxMessage(enabled),
            "websocket_close",
        };
        for (const auto& message : messages) {
            uint8_t mask[4];
            randomBytes(mask, sizeof(mask));
            if (!sendAll(fd, buildMaskedTextFrame(message, mask), error)) {
                ok = false;
                break;
            }
        }
    }
    if (ok) {
        // 読んでいないデータを残したまま閉じると RST になり、送った内容を相手が読む前に捨てられることがある。
        // 書き込みを閉じてから、相手が閉じるか少し時間がたつまで読み捨てる
        ::shutdown(fd, SHUT_WR);
        char buffer[1024];
        for (int i = 0; i < 20; ++i) {
            pollfd pfd {fd, POLLIN, 0};
            if (::poll(&pfd, 1, 25) <= 0) continue;
            if (::recv(fd, buffer, sizeof(buffer), 0) <= 0) break;
        }
    }
    ::close(fd);
    return ok;
}
