// 音楽・動画の操作（MPRIS）の実装。セッションバスにつなぐのは操作するときだけ（作業用スレッドで、毎回つないで閉じる）。
// 書き込むのは、選んだアプリへのメソッドの呼び出し（Play / Pause / Next / Previous）だけ。
#include "media_control.h"

#include "media.h"

#include <systemd/sd-bus.h>

#include <algorithm>
#include <cerrno>
#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <mutex>
#include <thread>

namespace {

/** プロパティを読む呼び出しを打ち切るまでの時間（µs）。 */
constexpr uint64_t kPropertyTimeoutUs = 500 * 1000;
/** 操作のメソッド（と名前の一覧）を打ち切るまでの時間（µs）。 */
constexpr uint64_t kCallTimeoutUs = 1000 * 1000;
/** 次の曲・前の曲のあと、曲名が変わるのを待つ上限（ms）と、読み直す間隔（ms）。 */
constexpr int kTitleWaitMs = 800;
constexpr int kTitlePollMs = 100;

constexpr const char* kObjectPath = "/org/mpris/MediaPlayer2";
constexpr const char* kPlayerInterface = "org.mpris.MediaPlayer2.Player";

/** 前に操作したアプリ（作業用スレッドと --test-action から使う）。 */
std::mutex gMemoryMutex;
MediaMemory gMemory;

/** セッションバスへのつながり（閉じ忘れないように）。 */
class UserBus {
public:
    UserBus() = default;
    UserBus(const UserBus&) = delete;
    UserBus& operator=(const UserBus&) = delete;
    ~UserBus() {
        if (bus_ != nullptr) sd_bus_flush_close_unref(bus_);
    }

    /**
     * つなぐ（DBUS_SESSION_BUS_ADDRESS か $XDG_RUNTIME_DIR/bus）。
     * @param error つなげなかったときの理由
     * @return つなげたら true
     */
    bool open(std::string& error) {
        const int r = sd_bus_open_user(&bus_);
        if (r < 0) {
            bus_ = nullptr;
            error = std::strerror(-r);
            return false;
        }
        // 時間を指定しない呼び出し（名前の一覧）も、止まったまま待ち続けないようにする
        sd_bus_set_method_call_timeout(bus_, kCallTimeoutUs);
        return true;
    }

    /** @return つながり */
    sd_bus* get() const { return bus_; }

private:
    sd_bus* bus_ = nullptr;
};

/**
 * sd-bus の失敗を文字列にする。
 * @param r 戻り値（負の errno）
 * @param e D-Bus のエラー（無ければ nullptr）
 * @return 説明
 */
std::string errorText(int r, const sd_bus_error* e) {
    if (e != nullptr && sd_bus_error_is_set(e) && e->message != nullptr) return e->message;
    return std::strerror(r < 0 ? -r : r);
}

/**
 * MPRIS のアプリのバス名を集める（名前の順。playerctld は除く）。
 * @param bus つながり
 * @param names 書き込み先
 * @param error 読めなかったときの理由
 * @return 読めたら true
 */
bool listPlayerNames(sd_bus* bus, std::vector<std::string>& names, std::string& error) {
    char** acquired = nullptr;
    const int r = sd_bus_list_names(bus, &acquired, nullptr);
    if (r < 0) {
        error = errorText(r, nullptr);
        return false;
    }
    for (char** p = acquired; p != nullptr && *p != nullptr; ++p) {
        if (isMediaPlayerName(*p)) names.push_back(*p);
        std::free(*p);
    }
    std::free(acquired);
    std::sort(names.begin(), names.end());
    return true;
}

/**
 * 値（v）の中身が 1 文字の型 sig なら読む。違う型なら読み飛ばす。
 * @param m メッセージ（v の手前）
 * @param sig 型（'s' なら const char*、'b' なら int に書く）
 * @param out 書き込み先
 * @return 読めたら 1、読み飛ばしたら 0、壊れていたら負
 */
int readVariantBasic(sd_bus_message* m, char sig, void* out) {
    char type = 0;
    const char* contents = nullptr;
    int r = sd_bus_message_peek_type(m, &type, &contents);
    if (r < 0) return r;
    if (r == 0 || type != SD_BUS_TYPE_VARIANT || contents == nullptr || contents[0] != sig || contents[1] != '\0') {
        r = sd_bus_message_skip(m, "v");
        return r < 0 ? r : 0;
    }
    r = sd_bus_message_enter_container(m, SD_BUS_TYPE_VARIANT, contents);
    if (r < 0) return r;
    r = sd_bus_message_read_basic(m, sig, out);
    if (r < 0) return r;
    r = sd_bus_message_exit_container(m);
    return r < 0 ? r : 1;
}

/**
 * Metadata（v の中の a{sv}）から xesam:title を読む。形が違えば読み飛ばす。
 * @param m メッセージ（v の手前）
 * @param title 書き込み先
 * @return 0 以上なら読めた（読み飛ばした）、負なら壊れていた
 */
int readTitle(sd_bus_message* m, std::string& title) {
    char type = 0;
    const char* contents = nullptr;
    int r = sd_bus_message_peek_type(m, &type, &contents);
    if (r < 0) return r;
    if (r == 0 || type != SD_BUS_TYPE_VARIANT || contents == nullptr || std::strcmp(contents, "a{sv}") != 0) {
        r = sd_bus_message_skip(m, "v");
        return r < 0 ? r : 0;
    }
    if ((r = sd_bus_message_enter_container(m, SD_BUS_TYPE_VARIANT, "a{sv}")) < 0) return r;
    if ((r = sd_bus_message_enter_container(m, SD_BUS_TYPE_ARRAY, "{sv}")) < 0) return r;
    while ((r = sd_bus_message_enter_container(m, SD_BUS_TYPE_DICT_ENTRY, "sv")) > 0) {
        const char* key = nullptr;
        if ((r = sd_bus_message_read_basic(m, 's', &key)) < 0) return r;
        if (key != nullptr && std::strcmp(key, "xesam:title") == 0) {
            const char* text = nullptr;
            r = readVariantBasic(m, 's', &text);
            if (r > 0 && text != nullptr) title = text;
        } else {
            r = sd_bus_message_skip(m, "v");
        }
        if (r < 0) return r;
        if ((r = sd_bus_message_exit_container(m)) < 0) return r;
    }
    if (r < 0) return r;
    if ((r = sd_bus_message_exit_container(m)) < 0) return r;
    return sd_bus_message_exit_container(m);
}

/**
 * アプリの様子（org.mpris.MediaPlayer2.Player のプロパティ全部）を読む。
 * @param bus つながり
 * @param player 書き込み先（busName を入れておく）
 * @param error 読めなかったときの理由
 * @return 読めたら true
 */
bool readPlayer(sd_bus* bus, MediaPlayer& player, std::string& error) {
    sd_bus_message* call = nullptr;
    sd_bus_message* reply = nullptr;
    sd_bus_error e = SD_BUS_ERROR_NULL;
    int r = sd_bus_message_new_method_call(bus, &call, player.busName.c_str(), kObjectPath,
                                           "org.freedesktop.DBus.Properties", "GetAll");
    if (r >= 0) r = sd_bus_message_append(call, "s", kPlayerInterface);
    if (r >= 0) r = sd_bus_call(bus, call, kPropertyTimeoutUs, &e, &reply);
    sd_bus_message_unref(call);
    if (r < 0) {
        error = errorText(r, &e);
        sd_bus_error_free(&e);
        return false;
    }
    r = sd_bus_message_enter_container(reply, SD_BUS_TYPE_ARRAY, "{sv}");
    while (r >= 0 && (r = sd_bus_message_enter_container(reply, SD_BUS_TYPE_DICT_ENTRY, "sv")) > 0) {
        const char* key = nullptr;
        if ((r = sd_bus_message_read_basic(reply, 's', &key)) < 0) break;
        const std::string name = key != nullptr ? key : "";
        int flag = 0;
        if (name == "PlaybackStatus") {
            const char* text = nullptr;
            r = readVariantBasic(reply, 's', &text);
            if (r > 0 && text != nullptr) player.status = text;
        } else if (name == "Metadata") {
            r = readTitle(reply, player.title);
        } else if (name == "CanControl" || name == "CanPlay" || name == "CanPause" || name == "CanGoNext" ||
                   name == "CanGoPrevious") {
            r = readVariantBasic(reply, 'b', &flag);
            if (r > 0) {
                const bool value = flag != 0;
                if (name == "CanControl") player.canControl = value;
                if (name == "CanPlay") player.canPlay = value;
                if (name == "CanPause") player.canPause = value;
                if (name == "CanGoNext") player.canGoNext = value;
                if (name == "CanGoPrevious") player.canGoPrevious = value;
            }
        } else {
            r = sd_bus_message_skip(reply, "v");
        }
        if (r < 0) break;
        r = sd_bus_message_exit_container(reply);
    }
    sd_bus_message_unref(reply);
    if (r < 0) {
        error = std::string("プロパティの形が違います: ") + std::strerror(-r);
        return false;
    }
    return true;
}

/**
 * 引数の無いメソッドを呼ぶ（Play / Pause / Next / Previous）。
 * @param bus つながり
 * @param busName アプリのバス名
 * @param method メソッド
 * @param error 呼べなかったときの理由
 * @return 呼べたら true
 */
bool callPlayer(sd_bus* bus, const std::string& busName, const std::string& method, std::string& error) {
    sd_bus_message* call = nullptr;
    sd_bus_message* reply = nullptr;
    sd_bus_error e = SD_BUS_ERROR_NULL;
    int r = sd_bus_message_new_method_call(bus, &call, busName.c_str(), kObjectPath, kPlayerInterface, method.c_str());
    if (r >= 0) r = sd_bus_call(bus, call, kCallTimeoutUs, &e, &reply);
    sd_bus_message_unref(call);
    sd_bus_message_unref(reply);
    if (r < 0) {
        error = errorText(r, &e);
        sd_bus_error_free(&e);
        return false;
    }
    return true;
}

/**
 * 失敗の結果を作る。
 * @param reason 理由のコード
 * @param detail ログ用の説明
 * @return 結果
 */
ActionOutcome failure(const std::string& reason, const std::string& detail) {
    ActionOutcome out;
    out.reason = reason;
    out.detail = detail;
    return out;
}

/**
 * ログ用のアプリの名前（バス名から頭を除いたもの。"chromium.instance2" など）。
 * @param busName バス名
 * @return 名前
 */
std::string shortName(const std::string& busName) {
    const std::string prefix = kMprisPrefix;
    return busName.compare(0, prefix.size(), prefix) == 0 ? busName.substr(prefix.size()) : busName;
}

}  // namespace

ActionOutcome runMediaAction(ActionType type) {
    UserBus bus;
    std::string error;
    if (!bus.open(error)) return failure("failed", "セッションバスにつなげません: " + error);
    std::vector<std::string> names;
    if (!listPlayerNames(bus.get(), names, error)) return failure("failed", "セッションバスの名前を読めません: " + error);
    std::vector<MediaPlayer> players;
    std::string skipped;
    for (const std::string& name : names) {
        MediaPlayer player;
        player.busName = name;
        if (readPlayer(bus.get(), player, error)) {
            players.push_back(player);
        } else {
            skipped += "（" + shortName(name) + " は読めません: " + error + "）";
        }
    }
    MediaMemory memory;
    {
        std::lock_guard<std::mutex> lock(gMemoryMutex);
        memory = gMemory;
    }
    const int index = chooseMediaPlayer(players, memory);
    if (index < 0) {
        return failure(kReasonNoPlayer, "再生中・一時停止中のアプリがありません（MPRIS のアプリ " +
                                            std::to_string(names.size()) + " 個）" + skipped);
    }
    const MediaPlayer& player = players[static_cast<size_t>(index)];
    const MediaCall call = mediaCallFor(type, player);
    const std::string who = shortName(player.busName) + "（" + (player.status.empty() ? "?" : player.status) + "）";
    if (call.method.empty()) return failure(kReasonMediaUnsupported, who + " はこの操作に対応していません（Can… が false）");
    if (!callPlayer(bus.get(), player.busName, call.method, error)) {
        return failure("failed", who + " に " + call.method + " を送れません: " + error);
    }
    {
        std::lock_guard<std::mutex> lock(gMemoryMutex);
        rememberMediaPlayer(gMemory, player.busName, call.value);
    }
    ActionOutcome out;
    out.ok = true;
    out.value = call.value;
    out.detail = who + " に " + call.method + " を送りました";
    if (type == ActionType::MediaPlayPause) {
        out.track = player.title;
        return out;
    }
    // 次の曲・前の曲: 曲名が変わるまで少し待つ（作業用スレッドなので、メインループは止まらない）
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(kTitleWaitMs);
    while (std::chrono::steady_clock::now() < deadline) {
        std::this_thread::sleep_for(std::chrono::milliseconds(kTitlePollMs));
        MediaPlayer after;
        after.busName = player.busName;
        if (!readPlayer(bus.get(), after, error)) break;
        if (!after.title.empty() && after.title != player.title) {
            out.track = after.title;
            break;
        }
    }
    if (out.track.empty()) out.detail += "（曲名はまだ変わっていません）";
    return out;
}
