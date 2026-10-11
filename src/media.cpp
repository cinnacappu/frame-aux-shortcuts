// 音楽・動画の操作の、D-Bus を使わない部分の実装。
#include "media.h"

namespace {

/**
 * 一覧から条件に合うものを選ぶ（前に操作したものがあればそれ、無ければ並びの最初）。
 * @param players アプリの一覧
 * @param status 選ぶ PlaybackStatus
 * @param preferred 優先するバス名（空なら優先しない）
 * @return 選んだ番号（無ければ -1）
 */
int pickByStatus(const std::vector<MediaPlayer>& players, const char* status, const std::string& preferred) {
    int first = -1;
    for (size_t i = 0; i < players.size(); ++i) {
        const MediaPlayer& player = players[i];
        if (!player.canControl || player.status != status) continue;
        if (!preferred.empty() && player.busName == preferred) return static_cast<int>(i);
        if (first < 0) first = static_cast<int>(i);
    }
    return first;
}

}  // namespace

bool isMediaAction(ActionType type) {
    return type == ActionType::MediaPlayPause || type == ActionType::MediaNext || type == ActionType::MediaPrevious;
}

bool isMediaPlayerName(const std::string& name) {
    const std::string prefix = kMprisPrefix;
    return name.size() > prefix.size() && name.compare(0, prefix.size(), prefix) == 0 && name != kPlayerctldName;
}

int chooseMediaPlayer(const std::vector<MediaPlayer>& players, const MediaMemory& memory) {
    const int playing = pickByStatus(players, "Playing", memory.lastControlled);
    if (playing >= 0) return playing;
    if (!memory.lastPaused.empty()) {
        for (size_t i = 0; i < players.size(); ++i) {
            const MediaPlayer& player = players[i];
            if (player.canControl && player.status == "Paused" && player.busName == memory.lastPaused) return static_cast<int>(i);
        }
    }
    return pickByStatus(players, "Paused", memory.lastControlled);
}

MediaCall mediaCallFor(ActionType type, const MediaPlayer& player) {
    MediaCall call;
    switch (type) {
        case ActionType::MediaPlayPause:
            if (player.status == "Playing") {
                call.value = kMediaPaused;
                if (player.canPause) call.method = "Pause";
            } else {
                call.value = kMediaPlaying;
                if (player.canPlay) call.method = "Play";
            }
            break;
        case ActionType::MediaNext:
            call.value = kMediaNext;
            if (player.canGoNext) call.method = "Next";
            break;
        case ActionType::MediaPrevious:
            call.value = kMediaPrevious;
            if (player.canGoPrevious) call.method = "Previous";
            break;
        default: break;
    }
    return call;
}

void rememberMediaPlayer(MediaMemory& memory, const std::string& busName, const std::string& value) {
    memory.lastControlled = busName;
    if (value == kMediaPaused) {
        memory.lastPaused = busName;
    } else if (value == kMediaPlaying) {
        memory.lastPaused.clear();  // 再生に戻したので、次の再生・一時停止は再生中のものを選ぶ
    }
}
