// 手応えの音の実装。
#include "sound.h"

#include "command.h"

#include <unistd.h>

#include <cstdio>

void playFeedbackSound(const SoundSettings& settings, bool success) {
    if (!settings.enabled) return;
    const std::string& path = success ? settings.success : settings.failure;
    if (path.empty()) return;
    if (::access(path.c_str(), R_OK) != 0) {
        std::fprintf(stderr, "[音] %s を読めないので鳴らしません\n", path.c_str());
        return;
    }
    std::string error;
    if (!spawnDetached({"pw-play", path}, error)) {
        std::fprintf(stderr, "[音] pw-play を起動できません: %s\n", error.c_str());
    }
}
