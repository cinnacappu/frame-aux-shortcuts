// SteamVR への接続の実装。
#include "vr_client.h"

#include <openvr.h>

#include <cstdio>
#include <string>

VrClient::~VrClient() {
    shutdown();
}

VrClient::ConnectResult VrClient::connect(std::string& message) {
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
    // 2) 視界の表示（オーバーレイ）を出せるよう、オーバーレイ型でつなぎ直す（frame-perf-overlay と同じ）
    vr::VR_Init(&error, vr::VRApplication_Overlay);
    if (error != vr::VRInitError_None) {
        message = vr::VR_GetVRInitErrorAsEnglishDescription(error);
        return ConnectResult::Error;
    }
    if (vr::VRSystem() == nullptr || vr::VRSettings() == nullptr || vr::VROverlay() == nullptr) {
        message = "OpenVR のインターフェースを取れません";
        vr::VR_Shutdown();
        return ConnectResult::Error;
    }
    connected_ = true;
    return ConnectResult::Ok;
}

void VrClient::pollEvents() {
    if (!connected_) return;
    bool quit = false;
    vr::VREvent_t event {};
    while (vr::VRSystem()->PollNextEvent(&event, sizeof(event))) {
        switch (event.eventType) {
            case vr::VREvent_Quit: quit = true; break;
            // アプリが動いている間は room view の状態が設定に残らないので、イベントで追う
            case vr::VREvent_RoomViewShown:
                if (!roomViewKnown_ || !roomViewShown_) std::fprintf(stderr, "[VR] room view が表示されました\n");
                rememberRoomView(true);
                break;
            case vr::VREvent_RoomViewHidden:
                if (!roomViewKnown_ || roomViewShown_) std::fprintf(stderr, "[VR] room view が隠れました\n");
                rememberRoomView(false);
                break;
            // スクリーンショット（requestScreenshot で頼んだもの）の結果。vrcompositor から来る
            case vr::VREvent_ScreenshotTaken: screenshotResults_[event.data.screenshot.handle] = true; break;
            case vr::VREvent_ScreenshotFailed: screenshotResults_[event.data.screenshot.handle] = false; break;
            default: break;
        }
    }
    if (quit && !quitRequested_) {
        std::fprintf(stderr, "[VR] SteamVR から終了の知らせが来ました\n");
        vr::VRSystem()->AcknowledgeQuit_Exiting();
        quitRequested_ = true;
    }
}

uint32_t VrClient::sceneProcessId() {
    if (!connected_ || vr::VRApplications() == nullptr) return 0;
    return vr::VRApplications()->GetCurrentSceneProcessId();
}

bool VrClient::getFloat(const char* section, const char* key, float& value, std::string& error) {
    if (!connected_) {
        error = "SteamVR につながっていません";
        return false;
    }
    vr::EVRSettingsError settingsError = vr::VRSettingsError_None;
    const float read = vr::VRSettings()->GetFloat(section, key, &settingsError);
    if (settingsError != vr::VRSettingsError_None) {
        error = std::string("GetFloat: ") + vr::VRSettings()->GetSettingsErrorNameFromEnum(settingsError);
        return false;
    }
    value = read;
    return true;
}

bool VrClient::getBool(const char* section, const char* key, bool& value, std::string& error) {
    if (!connected_) {
        error = "SteamVR につながっていません";
        return false;
    }
    vr::EVRSettingsError settingsError = vr::VRSettingsError_None;
    const bool read = vr::VRSettings()->GetBool(section, key, &settingsError);
    if (settingsError != vr::VRSettingsError_None) {
        error = std::string("GetBool: ") + vr::VRSettings()->GetSettingsErrorNameFromEnum(settingsError);
        return false;
    }
    value = read;
    return true;
}

bool VrClient::setFloat(const char* section, const char* key, float value, std::string& error) {
    if (!connected_) {
        error = "SteamVR につながっていません";
        return false;
    }
    vr::EVRSettingsError settingsError = vr::VRSettingsError_None;
    vr::VRSettings()->SetFloat(section, key, value, &settingsError);
    if (settingsError != vr::VRSettingsError_None) {
        error = std::string("SetFloat: ") + vr::VRSettings()->GetSettingsErrorNameFromEnum(settingsError);
        return false;
    }
    return true;
}

bool VrClient::showDashboard(std::string& error) {
    if (!connected_) {
        error = "SteamVR につながっていません";
        return false;
    }
    // 引数は「開いたときに見せるオーバーレイのキー」。特定のタブを指定せずに開くので空文字列を渡す
    // （nullptr は IPC の文字列の扱いが分からないので避ける）
    vr::VROverlay()->ShowDashboard("");
    return true;
}

bool VrClient::requestScreenshot(const std::string& previewBase, const std::string& vrBase, uint32_t& handle,
                                 std::string& error) {
    if (!connected_ || vr::VRScreenshots() == nullptr) {
        error = "SteamVR につながっていません";
        return false;
    }
    vr::ScreenshotHandle_t h = vr::k_unScreenshotHandleInvalid;
    const vr::EVRScreenshotError result =
        vr::VRScreenshots()->RequestScreenshot(&h, vr::VRScreenshotType_Mono, previewBase.c_str(), vrBase.c_str());
    if (result != vr::VRScreenshotError_None || h == vr::k_unScreenshotHandleInvalid) {
        error = "RequestScreenshot: エラー " + std::to_string(static_cast<int>(result));
        return false;
    }
    handle = h;
    return true;
}

bool VrClient::submitScreenshot(uint32_t handle, const std::string& path, std::string& error) {
    if (!connected_ || vr::VRScreenshots() == nullptr) {
        error = "SteamVR につながっていません";
        return false;
    }
    // Mono では VR 用の画像が無いので、VR 用にも同じ PNG を渡す
    const vr::EVRScreenshotError result =
        vr::VRScreenshots()->SubmitScreenshot(handle, vr::VRScreenshotType_Mono, path.c_str(), path.c_str());
    if (result != vr::VRScreenshotError_None) {
        error = "SubmitScreenshot: エラー " + std::to_string(static_cast<int>(result));
        return false;
    }
    return true;
}

bool VrClient::screenshotResult(uint32_t handle, bool& ok) {
    const auto it = screenshotResults_.find(handle);
    if (it == screenshotResults_.end()) return false;
    ok = it->second;
    screenshotResults_.erase(it);
    return true;
}

// head_pointer.h は OpenVR を使わないので、「機器が無い」の番号を OpenVR と照らし合わせておく
static_assert(kNoTrackedDevice == vr::k_unTrackedDeviceIndexInvalid, "機器が無いときの番号が OpenVR と違います");

bool VrClient::readPointer(PointerSample& sample) {
    if (!connected_) return false;
    PointerSample read;
    // 0 番はヘッドセット。電源を切ったコントローラーは、一覧に残っていても IsTrackedDeviceConnected が false になる
    for (vr::TrackedDeviceIndex_t i = vr::k_unTrackedDeviceIndex_Hmd + 1; i < vr::k_unMaxTrackedDeviceCount; ++i) {
        if (vr::VRSystem()->GetTrackedDeviceClass(i) == vr::TrackedDeviceClass_Controller &&
            vr::VRSystem()->IsTrackedDeviceConnected(i)) {
            ++read.controllers;
        }
    }
    read.dashboardVisible = vr::VROverlay()->IsDashboardVisible();
    read.dashboardPointer = vr::VROverlay()->GetPrimaryDashboardDevice();
    sample = read;
    return true;
}

void VrClient::shutdown() {
    if (!connected_) return;
    vr::VR_Shutdown();
    connected_ = false;
}
