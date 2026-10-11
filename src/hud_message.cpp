// 視界のお知らせの中身の決め方。
#include "hud_message.h"

#include "i18n.h"
#include "osc.h"

std::string reasonText(Language language, const std::string& reason) {
    const UiText& t = uiText(language);
    if (reason == "no_steamvr") return t.reasonNoSteamVr;
    if (reason == "room_view_api") return t.reasonRoomViewApi;
    if (reason == "app_missing") return t.reasonPerfMissing;
    if (reason == "app_stopped") return t.reasonPerfStopped;
    if (reason == "no_command") return t.reasonNoCommand;
    if (reason == "no_pc") return t.reasonNoPc;
    return t.reasonFailed;
}

namespace {

/**
 * 組み込みの OSC の送信の結果か（値が "osc:<アドレス>"）。
 * @param press 押し方の結果
 * @param address そうならアドレスの書き込み先
 * @return そうなら true
 */
bool oscAddressOf(const PressRecord& press, std::string& address) {
    const std::string prefix = kOscValuePrefix;
    if (press.action != ActionType::Command || press.value.compare(0, prefix.size(), prefix) != 0) return false;
    address = press.value.substr(prefix.size());
    return true;
}

}  // namespace

bool makeHudToast(const PressRecord& press, Language language, HudToast& toast) {
    const int slot = press.slot();
    if (slot < 0 || press.action == ActionType::None) return false;
    const UiText& t = uiText(language);
    HudToast out;
    out.sub = slotLabel(language, slot);
    std::string oscAddress;
    if (oscAddressOf(press, oscAddress)) {
        const bool voice = oscAddress == kVrchatVoiceAddress;
        if (!press.ok) {
            out.error = true;
            out.tone = HudToast::Tone::Danger;
            out.title = voice ? t.failVrcVoice : t.failOsc;
            out.sub += t.subSep + reasonText(language, press.reason);
        } else {
            out.icon = voice ? Icon::Mic : Icon::Terminal;
            out.tone = HudToast::Tone::Neutral;
            out.title = voice ? std::string(t.hudVrcVoice) : formatText(t.hudOscFormat, oscAddress);
        }
        toast = out;
        return true;
    }
    if (!press.ok) {
        out.error = true;
        out.tone = HudToast::Tone::Danger;
        switch (press.action) {
            case ActionType::MicMuteToggle: out.title = t.failMic; break;
            case ActionType::BrightnessCycle: out.title = t.failBright; break;
            case ActionType::RoomViewToggle: out.title = t.failPass; break;
            case ActionType::Screenshot: out.title = t.failShot; break;
            case ActionType::PerfOverlayToggle: out.title = t.failPerf; break;
            case ActionType::DashboardOpen: out.title = t.failDash; break;
            case ActionType::OriginalAux: out.title = t.failOriginal; break;
            case ActionType::Command: out.title = t.failCmd; break;
            case ActionType::None: return false;
        }
        out.sub += t.subSep + reasonText(language, press.reason);
        toast = out;
        return true;
    }
    switch (press.action) {
        case ActionType::MicMuteToggle:
            if (press.value == "muted") {
                out.icon = Icon::MicOff;
                out.tone = HudToast::Tone::Danger;
                out.title = t.hudMicMuted;
            } else if (press.value == "unmuted") {
                out.icon = Icon::Mic;
                out.tone = HudToast::Tone::Success;
                out.title = t.hudMicOn;
            } else {
                out.icon = Icon::Mic;
                out.tone = HudToast::Tone::Neutral;
                out.title = t.hudMicToggled;
            }
            break;
        case ActionType::BrightnessCycle:
            out.icon = Icon::Sun;
            out.tone = HudToast::Tone::Accent;
            out.title = formatText(t.hudBrightFormat, press.value);
            break;
        case ActionType::RoomViewToggle:
            out.icon = Icon::Eye;
            out.tone = press.value == "on" ? HudToast::Tone::Accent : HudToast::Tone::Neutral;
            out.title = press.value == "on" ? t.hudPassOn : t.hudPassOff;
            break;
        case ActionType::Screenshot:
            out.icon = Icon::Camera;
            out.tone = HudToast::Tone::Accent;
            out.title = t.hudShot;
            break;
        case ActionType::PerfOverlayToggle:
            out.icon = Icon::Chart;
            out.tone = press.value == "hidden" ? HudToast::Tone::Neutral : HudToast::Tone::Accent;
            out.title = press.value == "shown" ? t.hudPerfShown : (press.value == "hidden" ? t.hudPerfHidden : t.hudPerfToggled);
            break;
        case ActionType::DashboardOpen: return false;  // 開いたダッシュボードが手応えになる
        case ActionType::OriginalAux: return false;  // 開く・クリックした結果が手応えになる（失敗だけ出す）
        case ActionType::Command:
            out.icon = Icon::Terminal;
            out.tone = HudToast::Tone::Neutral;
            out.title = press.value.empty() ? t.hudCmd : press.value;  // 実行したコマンドの名前
            out.sub += t.subSep + std::string(t.hudCmd);
            break;
        case ActionType::None: return false;
    }
    toast = out;
    return true;
}

HudToast makeHandBackToast(bool handedBack, Language language) {
    const UiText& t = uiText(language);
    HudToast out;
    out.icon = Icon::Pointer;
    if (handedBack) {
        out.tone = HudToast::Tone::Accent;
        out.title = t.hudHandBack;
        out.sub = t.hudHandBackSub;
    } else {
        // 戻したわけ（コントローラーがつながった・設定をオフにした）が 2 つあるので、2 行目は書かない
        out.tone = HudToast::Tone::Neutral;
        out.title = t.hudShortcutsBack;
    }
    return out;
}
