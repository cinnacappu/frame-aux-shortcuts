// 視界の表示のオーバーレイの実装。
#include "hud_overlay.h"

#include "hud_render.h"

#include "openvr.h"

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <string>
#include <thread>

namespace {

const std::string kToastKey = "sasaken.frame-aux-shortcuts.toast";
const std::string kBadgeKey = "sasaken.frame-aux-shortcuts.mute";
/** 表示の寸法の 1px の大きさ（m）。頭から 0.5m 先で、frame-perf-overlay の性能パネルとほぼ同じ文字の大きさ。 */
constexpr float kMetersPerCssPx = 0.0004f;
/** 頭からの距離（m）。 */
constexpr float kDistanceM = 0.5f;
/** お知らせの高さ（視界の下寄り、m）。 */
constexpr float kToastY = -0.11f;
/** ミュート中の表示の高さ（視界の上寄り、m）。 */
constexpr float kBadgeY = 0.13f;
/** 消える間に透明度を変える間隔（秒）。 */
constexpr double kFadeStepSec = 1.0 / 30.0;
/** 終了時、オーバーレイを消してから VR_Shutdown まで待つ時間。 */
constexpr int kShutdownWaitMs = 400;

/**
 * オーバーレイのエラーを、成功以外ならログに出す。
 * @param what 何をしたときか
 * @param error エラー
 * @return 成功なら true
 */
bool check(const char* what, vr::EVROverlayError error) {
    if (error == vr::VROverlayError_None) return true;
    std::fprintf(stderr, "[視界] %s に失敗: %s\n", what, vr::VROverlay()->GetOverlayErrorNameFromEnum(error));
    return false;
}

/**
 * 頭の正面に置く変換（回転なし。オーバーレイの表は +z、頭の方を向く）。
 * @param y 高さ（m）
 * @return 変換
 */
vr::HmdMatrix34_t headTransform(float y) {
    vr::HmdMatrix34_t m {};
    m.m[0][0] = 1.0f;
    m.m[1][1] = 1.0f;
    m.m[2][2] = 1.0f;
    m.m[1][3] = y;
    m.m[2][3] = -kDistanceM;
    return m;
}

/**
 * オーバーレイを作って頭に付ける。
 * @param key キー
 * @param name 名前
 * @param widthM 幅（m）
 * @param y 高さ（m）
 * @param handle 作れたときの書き込み先
 * @return 作れたら true
 */
bool createHeadOverlay(const char* key, const char* name, float widthM, float y, uint64_t& handle) {
    vr::IVROverlay* overlay = vr::VROverlay();
    vr::VROverlayHandle_t h = vr::k_ulOverlayHandleInvalid;
    if (!check("CreateOverlay", overlay->CreateOverlay(key, name, &h))) return false;
    handle = h;
    check("SetOverlayWidthInMeters", overlay->SetOverlayWidthInMeters(h, widthM));
    const vr::HmdMatrix34_t transform = headTransform(y);
    check("SetOverlayTransformTrackedDeviceRelative",
          overlay->SetOverlayTransformTrackedDeviceRelative(h, vr::k_unTrackedDeviceIndex_Hmd, &transform));
    check("SetOverlaySortOrder", overlay->SetOverlaySortOrder(h, 200));
    return true;
}

}  // namespace

bool HudOverlay::ensure() {
    if (ready_) return true;
    if (tried_) return false;
    tried_ = true;
    std::string message;
    if (!vulkan_.init(message)) {
        std::fprintf(stderr, "[視界] Vulkan の準備に失敗: %s（視界の表示は出しません）\n", message.c_str());
        return false;
    }
    if (!toastTexture_.create(vulkan_, HudRenderer::kToastW, HudRenderer::kToastH, message) ||
        !badgeTexture_.create(vulkan_, HudRenderer::kBadgeW, HudRenderer::kBadgeH, message)) {
        std::fprintf(stderr, "[視界] テクスチャを作れません: %s（視界の表示は出しません）\n", message.c_str());
        return false;
    }
    if (!createHeadOverlay((kToastKey + keySuffix_).c_str(), "aux shortcuts notice",
                           kMetersPerCssPx * HudRenderer::kToastCssW, kToastY, toastHandle_) ||
        !createHeadOverlay((kBadgeKey + keySuffix_).c_str(), "aux shortcuts mute",
                           kMetersPerCssPx * HudRenderer::kBadgeCssW, kBadgeY, badgeHandle_)) {
        return false;
    }
    ready_ = true;
    std::fprintf(stderr, "[視界] オーバーレイを作りました\n");
    return true;
}

bool HudOverlay::showToast(const std::vector<uint8_t>& rgba, double now) {
    if (!ensure()) return false;
    std::string message;
    if (!toastTexture_.update(toastHandle_, rgba.data(), message)) {
        std::fprintf(stderr, "[視界] お知らせを送れません: %s\n", message.c_str());
        return false;
    }
    vr::IVROverlay* overlay = vr::VROverlay();
    check("SetOverlayAlpha", overlay->SetOverlayAlpha(toastHandle_, 1.0f));
    if (!toastVisible_) check("ShowOverlay", overlay->ShowOverlay(toastHandle_));
    toastVisible_ = true;
    toastShownAt_ = now;
    nextTickAt_ = now + kToastHoldSec;
    return true;
}

void HudOverlay::hideToast() {
    if (!toastVisible_ || !ready_) return;
    check("HideOverlay", vr::VROverlay()->HideOverlay(toastHandle_));
    toastVisible_ = false;
    nextTickAt_ = std::numeric_limits<double>::infinity();
}

bool HudOverlay::setBadge(const std::vector<uint8_t>* rgba) {
    if (rgba == nullptr) {
        if (badgeVisible_ && ready_) check("HideOverlay", vr::VROverlay()->HideOverlay(badgeHandle_));
        badgeVisible_ = false;
        return true;
    }
    if (!ensure()) return false;
    std::string message;
    if (!badgeTexture_.update(badgeHandle_, rgba->data(), message)) {
        std::fprintf(stderr, "[視界] ミュート中の表示を送れません: %s\n", message.c_str());
        return false;
    }
    if (!badgeVisible_) check("ShowOverlay", vr::VROverlay()->ShowOverlay(badgeHandle_));
    badgeVisible_ = true;
    return true;
}

void HudOverlay::tick(double now) {
    if (!toastVisible_ || !ready_) {
        nextTickAt_ = std::numeric_limits<double>::infinity();
        return;
    }
    const double elapsed = now - toastShownAt_;
    if (elapsed < kToastHoldSec) {
        nextTickAt_ = toastShownAt_ + kToastHoldSec;
        return;
    }
    const double fade = (elapsed - kToastHoldSec) / kToastFadeSec;
    if (fade >= 1.0) {
        check("HideOverlay", vr::VROverlay()->HideOverlay(toastHandle_));
        toastVisible_ = false;
        nextTickAt_ = std::numeric_limits<double>::infinity();
        return;
    }
    vr::VROverlay()->SetOverlayAlpha(toastHandle_, static_cast<float>(1.0 - fade));
    nextTickAt_ = now + kFadeStepSec;
}

void HudOverlay::releaseOverlays() {
    if (toastHandle_ == 0 && badgeHandle_ == 0) return;
    vr::IVROverlay* overlay = vr::VROverlay();
    for (const uint64_t handle : {toastHandle_, badgeHandle_}) {
        if (handle == 0) continue;
        overlay->HideOverlay(handle);
        overlay->ClearOverlayTexture(handle);
        overlay->DestroyOverlay(handle);
    }
    toastHandle_ = 0;
    badgeHandle_ = 0;
    toastVisible_ = false;
    badgeVisible_ = false;
    ready_ = false;
    nextTickAt_ = std::numeric_limits<double>::infinity();
    // コンポジタが数フレーム回って、外したテクスチャを手放すのを待つ
    std::this_thread::sleep_for(std::chrono::milliseconds(kShutdownWaitMs));
    std::fprintf(stderr, "[視界] オーバーレイを片付けました\n");
}

void HudOverlay::releaseVulkan() {
    toastTexture_.destroy();
    badgeTexture_.destroy();
    vulkan_.destroy();
    tried_ = false;
}
