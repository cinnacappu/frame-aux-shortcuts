// スクリーンショットの保存先とファイル名の実装。
#include "screenshot.h"

#include "config.h"

#include <sys/stat.h>
#include <unistd.h>

#include <cerrno>
#include <cstring>

namespace {

/**
 * フォルダを（親も含めて）作る。
 * @param dir 作るフォルダ
 * @return できた（元からあった）なら true
 */
bool makeDirectories(const std::string& dir) {
    for (size_t pos = 1; pos <= dir.size(); ++pos) {
        if (pos != dir.size() && dir[pos] != '/') continue;
        const std::string part = dir.substr(0, pos);
        if (::mkdir(part.c_str(), 0755) != 0 && errno != EEXIST) return false;
    }
    return true;
}

}  // namespace

std::string screenshotFileName(const std::tm& when) {
    char name[64];
    std::strftime(name, sizeof(name), "screenshot_%Y-%m-%d_%H-%M-%S.png", &when);
    return name;
}

bool prepareScreenshotPaths(const std::string& dirSetting, const std::tm& when, ScreenshotPaths& paths,
                            std::string& error) {
    std::string dir = expandHome(dirSetting.empty() ? std::string(kDefaultScreenshotDir) : dirSetting);
    while (dir.size() > 1 && dir.back() == '/') dir.pop_back();
    if (!makeDirectories(dir)) {
        error = dir + " を作れません: " + std::strerror(errno);
        return false;
    }
    std::string name = screenshotFileName(when);
    for (int n = 2; ::access((dir + "/" + name).c_str(), F_OK) == 0; ++n) {
        if (n >= 100) {
            error = dir + " に同じ時刻のファイルが多すぎます";
            return false;
        }
        name = screenshotFileName(when);
        name.insert(name.size() - 4, "_" + std::to_string(n));
    }
    const std::string stem = name.substr(0, name.size() - 4);  // .png を外す（向こうが付ける）
    paths.previewBase = dir + "/" + stem;
    paths.vrBase = dir + "/." + stem + "_vr";
    paths.path = paths.previewBase + ".png";
    return true;
}
