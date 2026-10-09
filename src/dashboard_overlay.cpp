// 設定パネルの OpenVR まわりの実装（frame-perf-overlay の vr_overlay.cpp と同じ手順）。
#include "dashboard_overlay.h"

#include "openvr.h"

#include <dirent.h>
#include <fcntl.h>
#include <signal.h>
#include <unistd.h>

#include <cerrno>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>
#include <thread>

namespace {

constexpr const char* kDashboardKey = "sasaken.frame-aux-shortcuts.panel";
constexpr const char* kDashboardName = "aux Shortcuts";
// 1px あたりの大きさ（m）。frame-perf-overlay の設定パネルと同じ（1200px で 2.8m）
constexpr float kMetersPerPx = 2.8f / 1200.0f;
// 終了時、オーバーレイを消してから VR_Shutdown まで待つ時間（90Hz で約 36 フレーム）
constexpr int kShutdownWaitMs = 400;

/**
 * オーバーレイのエラーを、成功以外なら標準エラーに出す。
 * @param what 何をしたときか
 * @param error エラー
 * @return 成功なら true
 */
bool check(const char* what, vr::EVROverlayError error) {
    if (error == vr::VROverlayError_None) return true;
    std::fprintf(stderr, "[VR] %s に失敗: %s\n", what, vr::VROverlay()->GetOverlayErrorNameFromEnum(error));
    return false;
}

/**
 * /proc を一度だけ走査して vrserver の PID を探す。
 * @return 見つかった PID。無ければ -1
 */
int findVrserverPid() {
    DIR* proc = ::opendir("/proc");
    if (proc == nullptr) return -1;
    int found = -1;
    while (const dirent* entry = ::readdir(proc)) {
        const char* name = entry->d_name;
        if (name[0] < '0' || name[0] > '9') continue;
        const std::string commPath = std::string("/proc/") + name + "/comm";
        const int fd = ::open(commPath.c_str(), O_RDONLY | O_CLOEXEC);
        if (fd < 0) continue;
        char comm[64] = {};
        const ssize_t n = ::read(fd, comm, sizeof(comm) - 1);
        ::close(fd);
        if (n > 0 && std::strncmp(comm, "vrserver\n", 9) == 0) {
            found = std::atoi(name);
            break;
        }
    }
    ::closedir(proc);
    return found;
}

}  // namespace

DashboardOverlay::~DashboardOverlay() {
    shutdown();
}

DashboardOverlay::ConnectResult DashboardOverlay::connect(int width, int height, std::string& message) {
    if (connected_) return ConnectResult::Ok;
    // 1) Background 型で「SteamVR が動いているか」だけ確かめる（動いていなければ起動させない）
    vr::EVRInitError error = vr::VRInitError_None;
    vr::VR_Init(&error, vr::VRApplication_Background);
    if (error != vr::VRInitError_None) {
        message = vr::VR_GetVRInitErrorAsEnglishDescription(error);
        return error == vr::VRInitError_Init_NoServerForBackgroundApp ? ConnectResult::NotRunning
                                                                        : ConnectResult::Error;
    }
    vr::VR_Shutdown();
    // 2) オーバーレイ型でつなぎ直す
    vr::VR_Init(&error, vr::VRApplication_Overlay);
    if (error != vr::VRInitError_None) {
        message = vr::VR_GetVRInitErrorAsEnglishDescription(error);
        return ConnectResult::Error;
    }
    connected_ = true;

    // 3) Vulkan
    std::string vkMessage;
    if (!vulkan_.init(vkMessage)) {
        message = "Vulkan の準備に失敗: " + vkMessage;
        shutdown();
        return ConnectResult::Error;
    }

    // 4) ダッシュボードの設定パネルとサムネイル
    vr::IVROverlay* overlay = vr::VROverlay();
    vr::VROverlayHandle_t main = vr::k_ulOverlayHandleInvalid;
    vr::VROverlayHandle_t thumbnail = vr::k_ulOverlayHandleInvalid;
    const vr::EVROverlayError createError =
        overlay->CreateDashboardOverlay(kDashboardKey, kDashboardName, &main, &thumbnail);
    std::fprintf(stderr, "[VR] CreateDashboardOverlay -> %s\n", overlay->GetOverlayErrorNameFromEnum(createError));
    if (createError != vr::VROverlayError_None) {
        message = std::string("CreateDashboardOverlay: ") + overlay->GetOverlayErrorNameFromEnum(createError);
        shutdown();
        return ConnectResult::Error;
    }
    dashboardHandle_ = main;
    thumbnailHandle_ = thumbnail;
    height_ = height;
    check("SetOverlayWidthInMeters", overlay->SetOverlayWidthInMeters(main, kMetersPerPx * static_cast<float>(width)));
    check("SetOverlayInputMethod", overlay->SetOverlayInputMethod(main, vr::VROverlayInputMethod_Mouse));
    // マウス座標を画像の px にそろえる
    const vr::HmdVector2_t scale = {{static_cast<float>(width), static_cast<float>(height)}};
    check("SetOverlayMouseScale", overlay->SetOverlayMouseScale(main, &scale));
    // ダッシュボードの下のアイコンにホバーしたとき「閉じる」を出す（押されると VREvent_OverlayClosed）
    check("SetOverlayFlag(EnableControlBarClose)",
          overlay->SetOverlayFlag(main, vr::VROverlayFlags_EnableControlBarClose, true));
    // コントローラーのスティックで一覧をスクロールする（なめらかなスクロールのイベント VREvent_ScrollSmooth）
    check("SetOverlayFlag(SendVRSmoothScrollEvents)",
          overlay->SetOverlayFlag(main, vr::VROverlayFlags_SendVRSmoothScrollEvents, true));
    if (!settingsTexture_.create(vulkan_, width, height, vkMessage)) {
        std::fprintf(stderr, "[Vulkan] 設定パネルのテクスチャを作れません: %s\n", vkMessage.c_str());
    }
    vrserverPid_ = findVrserverPid();
    lastError_.clear();
    return ConnectResult::Ok;
}

void DashboardOverlay::shutdown() {
    if (!connected_) return;
    vr::IVROverlay* overlay = vr::VROverlay();
    std::fprintf(stderr, "[VR] 終了処理を始めます\n");
    if (dashboardHandle_ != 0) {
        overlay->ClearOverlayTexture(dashboardHandle_);
        overlay->ClearOverlayTexture(thumbnailHandle_);
        // サムネイルは設定パネルと一緒に消える
        check("DestroyOverlay", overlay->DestroyOverlay(dashboardHandle_));
        std::this_thread::sleep_for(std::chrono::milliseconds(kShutdownWaitMs));
    }
    dashboardHandle_ = 0;
    thumbnailHandle_ = 0;
    // OpenVR を閉じてから Vulkan の画像を壊す（OpenVR の決まり）
    vr::VR_Shutdown();
    connected_ = false;
    thumbnailTexture_.destroy();
    settingsTexture_.destroy();
    vulkan_.destroy();
    std::fprintf(stderr, "[VR] 終了処理が終わりました\n");
}

/**
 * キーボードのイベントの名前（ログ用）。
 * @param type イベントの種類
 * @return 名前
 */
static const char* keyboardEventName(uint32_t type) {
    switch (type) {
        case vr::VREvent_KeyboardClosed: return "KeyboardClosed";
        case vr::VREvent_KeyboardCharInput: return "KeyboardCharInput";
        case vr::VREvent_KeyboardDone: return "KeyboardDone";
        case vr::VREvent_KeyboardOpened_Global: return "KeyboardOpened_Global";
        case vr::VREvent_KeyboardClosed_Global: return "KeyboardClosed_Global";
        default: return "?";
    }
}

/**
 * キーボードのイベントか（1200〜1204）。
 * @param type イベントの種類
 * @return そうなら true
 */
static bool isKeyboardEvent(uint32_t type) {
    return type >= vr::VREvent_KeyboardClosed && type <= vr::VREvent_KeyboardClosed_Global;
}

/**
 * 今の時刻（秒、単調増加）。
 * @return 秒
 */
static double steadySeconds() {
    using namespace std::chrono;
    return duration<double>(steady_clock::now().time_since_epoch()).count();
}

/**
 * GetKeyboardText を読んで、戻り値と長さをログに出す。
 * @param why いつ読んだか（ログ用）
 * @return 読めた文字
 */
static std::string readKeyboardText(const char* why) {
    char text[1024] = {};
    const uint32_t ret = vr::VROverlay()->GetKeyboardText(text, sizeof(text));
    std::fprintf(stderr, "[キーボード] GetKeyboardText（%s）-> 戻り値 %u、長さ %zu バイト\n", why, ret, std::strlen(text));
    return text;
}

DashboardEvents DashboardOverlay::pollEvents(bool includeSystem) {
    DashboardEvents result;
    if (!connected_) return result;
    vr::VREvent_t event {};
    std::vector<vr::VREvent_t> fromOverlay;  // キーボードのイベント（オーバーレイのキュー）
    std::vector<vr::VREvent_t> fromSystem;   // キーボードのイベント（全体のキュー）
    /**
     * キーボードのイベントをすべてログに出す（どちらのキューか・overlayHandle・uUserValue・入力のバイト数）。
     * 打った中身（cNewInput の 16 進）は FRAME_AUX_KEYBOARD_DEBUG=1 のときだけ。
     * @param queue どちらのキューか
     * @param e イベント
     */
    const auto logKeyboard = [&](const char* queue, const vr::VREvent_t& e) {
        const std::string line = keyboardEventLogLine(
            queue, keyboardEventName(e.eventType), e.eventType, e.data.keyboard.overlayHandle, dashboardHandle_,
            e.data.keyboard.uUserValue, e.data.keyboard.cNewInput, sizeof(e.data.keyboard.cNewInput), keyboardDebugEnabled());
        std::fputs(line.c_str(), stderr);
    };
    if (dashboardHandle_ != 0) {
        while (vr::VROverlay()->PollNextOverlayEvent(dashboardHandle_, &event, sizeof(event))) {
            if (isKeyboardEvent(event.eventType)) {
                logKeyboard("オーバーレイ", event);
                fromOverlay.push_back(event);
                continue;
            }
            // マウス座標は左下が原点なので、上が原点になるよう反転する
            const double x = event.data.mouse.x;
            const double y = height_ - event.data.mouse.y;
            switch (event.eventType) {
                case vr::VREvent_MouseMove: result.pointer.push_back({PointerInput::Type::Move, x, y}); break;
                case vr::VREvent_MouseButtonDown:
                    if (event.data.mouse.button == vr::VRMouseButton_Left) {
                        result.pointer.push_back({PointerInput::Type::Down, x, y});
                    }
                    break;
                case vr::VREvent_MouseButtonUp:
                    if (event.data.mouse.button == vr::VRMouseButton_Left) {
                        result.pointer.push_back({PointerInput::Type::Up, x, y});
                    }
                    break;
                case vr::VREvent_FocusLeave: result.pointer.push_back({PointerInput::Type::Leave, 0, 0}); break;
                case vr::VREvent_ScrollSmooth:
                case vr::VREvent_ScrollDiscrete:
                    result.pointer.push_back({PointerInput::Type::Scroll, 0, event.data.scroll.ydelta});
                    break;
                case vr::VREvent_OverlayClosed:
                    std::fprintf(stderr, "[VR] ダッシュボードの「閉じる」が押されました\n");
                    result.closeRequested = true;
                    break;
                default: break;
            }
        }
    }
    if (includeSystem) {
        while (vr::VRSystem()->PollNextEvent(&event, sizeof(event))) {
            if (event.eventType == vr::VREvent_Quit) result.quit = true;
            if (isKeyboardEvent(event.eventType)) {
                logKeyboard("全体", event);
                fromSystem.push_back(event);
            }
        }
    }

    // キーボード: 同じイベントが両方のキューに来ることがあるので、オーバーレイのキューに来た回はそちらだけを使う。
    // 全体のキューは、このオーバーレイに開いたもの（overlayHandle が自分）だけ
    if (!fromOverlay.empty()) keyboardOverlayQueue_ = true;
    std::vector<vr::VREvent_t> keyboardEvents = fromOverlay;
    if (!keyboardOverlayQueue_) {
        for (const auto& e : fromSystem) {
            if (e.data.keyboard.overlayHandle == dashboardHandle_) keyboardEvents.push_back(e);
        }
    }
    /**
     * キーボードを終える（欄に入れる文字を決める）。
     * @param text 欄に入れる文字
     * @param why ログに出す理由
     */
    const auto finish = [&](const std::string& text, const char* why) { finishKeyboard(result, text, why); };
    for (const auto& e : keyboardEvents) {
        if (!keyboardOpen_) break;
        // 前に開いたキーボードの、遅れて届いたイベントは使わない（開き直した直後に閉じないように）。
        // uUserValue が 0 のもの（入っていないもの）は今の回のものとして扱う
        const uint64_t user = e.data.keyboard.uUserValue;
        if (user != 0 && (user >> 8) != keyboardSession_) {
            std::fprintf(stderr, "[キーボード] 前のキーボード（回 %llu）のイベントなので使いません（今は回 %llu）\n",
                         static_cast<unsigned long long>(user >> 8), static_cast<unsigned long long>(keyboardSession_));
            continue;
        }
        switch (e.eventType) {
            case vr::VREvent_KeyboardCharInput: {
                if (!keyboardMinimal_) break;  // 今までの方式では、Steam のキーボードの中の文字を完了で読む
                const bool done = keyboardBuffer_.feed(e.data.keyboard.cNewInput, sizeof(e.data.keyboard.cNewInput));
                result.keyboardChanged = true;
                result.keyboardField = keyboardField_;
                result.keyboardText = keyboardBuffer_.text();
                result.keyboardCursor = keyboardBuffer_.cursor();
                if (done) {
                    finish(keyboardBuffer_.text(), "改行");
                    vr::VROverlay()->HideKeyboard();
                }
                break;
            }
            case vr::VREvent_KeyboardDone: {
                // 完了: GetKeyboardText が空でなければそれ、空なら自分でためたもの
                const std::string text = readKeyboardText("Done");
                if (!text.empty()) {
                    finish(text, "Done・GetKeyboardText");
                } else {
                    finish(keyboardMinimal_ ? keyboardBuffer_.text() : keyboardPolled_, "Done・ためた文字");
                }
                break;
            }
            case vr::VREvent_KeyboardClosed:
            case vr::VREvent_KeyboardClosed_Global: {
                // B などで閉じた: ためた文字は欄に残す
                const std::string text = readKeyboardText("Closed");
                if (keyboardMinimal_) {
                    finish(keyboardBuffer_.text(), "Closed・ためた文字");
                } else {
                    finish(!text.empty() ? text : keyboardPolled_, "Closed・GetKeyboardText");
                }
                break;
            }
            default: break;
        }
    }
    // 開いている間は 0.5 秒ごとに GetKeyboardText を読み、長さが変わったらログに出す（今までの方式ではその場で欄にも出す）
    if (keyboardOpen_ && steadySeconds() >= keyboardNextPoll_) {
        keyboardNextPoll_ = steadySeconds() + 0.5;
        char text[1024] = {};
        const uint32_t ret = vr::VROverlay()->GetKeyboardText(text, sizeof(text));
        if (std::string(text) != keyboardPolled_) {
            std::fprintf(stderr, "[キーボード] 開いている間の GetKeyboardText -> 戻り値 %u、長さ %zu バイト（前は %zu）\n", ret,
                         std::strlen(text), keyboardPolled_.size());
            keyboardPolled_ = text;
            if (!keyboardMinimal_ && !keyboardPolled_.empty()) {
                result.keyboardChanged = true;
                result.keyboardField = keyboardField_;
                result.keyboardText = keyboardPolled_;
                result.keyboardCursor = keyboardPolled_.size();
            }
        }
    }
    if (result.quit) {
        std::fprintf(stderr, "[VR] SteamVR から終了の知らせが来ました\n");
        vr::VRSystem()->AcknowledgeQuit_Exiting();
    }
    return result;
}

void DashboardOverlay::finishKeyboard(DashboardEvents& result, const std::string& text, const char* why) {
    std::fprintf(stderr, "[キーボード] 終わりました（%s）。欄 %llu に %zu バイトを入れます\n", why,
                 static_cast<unsigned long long>(keyboardField_), text.size());
    result.keyboardChanged = true;
    result.keyboardFinished = true;
    result.keyboardField = keyboardField_;
    result.keyboardText = text;
    result.keyboardCursor = text == keyboardBuffer_.text() ? keyboardBuffer_.cursor() : text.size();
    keyboardOpen_ = false;
}

bool DashboardOverlay::retargetKeyboard(uint64_t field, const std::string& existing, size_t cursor) {
    if (!keyboardOpen_ || !keyboardMinimal_) return false;
    std::fprintf(stderr, "[キーボード] 入力先を欄 %llu から欄 %llu に切り替えます（開き直しません）。入力位置 %zu\n",
                 static_cast<unsigned long long>(keyboardField_), static_cast<unsigned long long>(field),
                 cursor == std::string::npos ? existing.size() : cursor);
    keyboardField_ = field;
    keyboardBuffer_.reset(existing);
    if (cursor != std::string::npos) keyboardBuffer_.setCursor(cursor);
    keyboardPolled_ = existing;
    return true;
}

DashboardEvents DashboardOverlay::hideKeyboard(const char* why) {
    DashboardEvents result;
    if (!keyboardOpen_) return result;
    std::string text = keyboardBuffer_.text();
    if (!keyboardMinimal_) {
        // 今までの方式では、文字は Steam のキーボードの中にある
        const std::string read = readKeyboardText("閉じる前");
        text = !read.empty() ? read : keyboardPolled_;
    }
    std::fprintf(stderr, "[キーボード] 閉じます（%s）\n", why);
    finishKeyboard(result, text, why);
    if (connected_) vr::VROverlay()->HideKeyboard();
    return result;
}

bool DashboardOverlay::settingsVisible() const {
    if (!connected_ || dashboardHandle_ == 0) return false;
    vr::IVROverlay* overlay = vr::VROverlay();
    // ダッシュボード全体が開いているだけでは見えている扱いにしない
    if (!overlay->IsDashboardVisible()) return false;
    return overlay->IsActiveDashboardOverlay(dashboardHandle_) && overlay->IsOverlayVisible(dashboardHandle_);
}

bool DashboardOverlay::showKeyboard(uint64_t field, const std::string& description, const std::string& existing,
                                    size_t cursor) {
    if (!connected_ || dashboardHandle_ == 0) return false;
    const uint64_t session = keyboardSession_ + 1;
    const uint64_t userValue = (session << 8) | (field & 0xFF);
    // Modal は付けない（付けるとパネルを押すたびに SteamVR がキーボードを閉じ、開き直しでちらつく）。
    // 矢印キーは確実に出す（入力位置を動かすため）
    uint32_t flags = static_cast<uint32_t>(vr::KeyboardFlag_ShowArrowKeys);
    if (keyboardMinimal_) flags |= static_cast<uint32_t>(vr::KeyboardFlag_Minimal);
    if (keyboardModal_) flags |= static_cast<uint32_t>(vr::KeyboardFlag_Modal);
    const vr::EVROverlayError error = vr::VROverlay()->ShowKeyboardForOverlay(
        dashboardHandle_, vr::k_EGamepadTextInputModeNormal, vr::k_EGamepadTextInputLineModeSingleLine, flags,
        description.c_str(), 512, existing.c_str(), userValue);
    std::fprintf(stderr,
                 "[キーボード] 開きます: 欄 %llu、回 %llu（uUserValue %llu）、方式 %s、flags 0x%x（Modal %s）、最初の文字 %zu バイト、"
                 "入力位置 %zu -> %s\n",
                 static_cast<unsigned long long>(field), static_cast<unsigned long long>(session),
                 static_cast<unsigned long long>(userValue),
                 keyboardMinimal_ ? "minimal（1 文字ずつ組み立てる）" : "buffered（今までの方式）", flags,
                 keyboardModal_ ? "あり" : "なし", existing.size(),
                 cursor == std::string::npos ? existing.size() : cursor, vr::VROverlay()->GetOverlayErrorNameFromEnum(error));
    if (error != vr::VROverlayError_None) return false;
    keyboardSession_ = session;
    keyboardOpen_ = true;
    keyboardField_ = field;
    keyboardOverlayQueue_ = false;
    // Minimal ではキーボードの中に最初の文字が出ないことがあるので、ためる文字列を今の値から始める
    keyboardBuffer_.reset(existing);
    if (cursor != std::string::npos) keyboardBuffer_.setCursor(cursor);  // 押した位置から入力する
    keyboardPolled_ = existing;
    keyboardNextPoll_ = steadySeconds() + 0.5;
    return true;
}

void DashboardOverlay::showSettings() {
    if (!connected_ || dashboardHandle_ == 0) return;
    vr::VROverlay()->ShowDashboard(kDashboardKey);
}

bool DashboardOverlay::steamVrAlive() const {
    if (vrserverPid_ <= 0) return true;  // 確かめられないときは生きている扱い
    return ::kill(vrserverPid_, 0) == 0 || errno == EPERM;
}

bool DashboardOverlay::submitThumbnail(const uint8_t* rgba, int size) {
    if (!connected_ || thumbnailHandle_ == 0) return false;
    std::string message;
    if (!thumbnailTexture_.ready() && !thumbnailTexture_.create(vulkan_, size, size, message)) {
        std::fprintf(stderr, "[Vulkan] サムネイルのテクスチャを作れません: %s\n", message.c_str());
        return false;
    }
    if (!thumbnailTexture_.update(thumbnailHandle_, rgba, message)) {
        std::fprintf(stderr, "[VR] サムネイルを送れません: %s\n", message.c_str());
        return false;
    }
    return true;
}

bool DashboardOverlay::submitSettings(const uint8_t* rgba) {
    if (!connected_ || dashboardHandle_ == 0 || !settingsTexture_.ready()) return false;
    std::string message;
    const bool ok = settingsTexture_.update(dashboardHandle_, rgba, message);
    if (message != lastError_) {
        if (!ok) std::fprintf(stderr, "[VR] 設定パネルを送れません: %s\n", message.c_str());
        lastError_ = message;
    }
    return ok;
}
