// スクリーンショットの保存先とファイル名。撮るのは OpenVR の公開 API（IVRScreenshots::RequestScreenshot）で、
// vrcompositor が「プレビューのファイル名 + .png」に VR の画面（1920×1080）を書く（vr_client.h）。
// ここは OpenVR を使わない部分（フォルダを作る・名前を決める）だけ。
#pragma once

#include <ctime>
#include <string>

/**
 * 保存するファイルの名前（screenshot_YYYY-MM-DD_HH-MM-SS.png）。
 * @param when 撮った時刻（地方時）
 * @return ファイルの名前
 */
std::string screenshotFileName(const std::tm& when);

/** RequestScreenshot に渡すファイル名（拡張子なし。向こうが .png を付ける）と、できあがるファイル。 */
struct ScreenshotPaths {
    std::string previewBase;  ///< プレビューのファイル名（拡張子なし）。これ + ".png" が保存したい画像になる
    std::string vrBase;       ///< VR 用のファイル名（拡張子なし。Mono では書かれないが、書かれたら消す）
    std::string path;         ///< できあがるファイル（previewBase + ".png"）
};

/**
 * 保存先のフォルダを作り（無ければ）、まだ無いファイル名を決める（同じ秒に 2 枚なら _2 を付ける）。
 * @param dir 保存先のフォルダ（~ で始めてよい。空なら既定）
 * @param when 撮る時刻（地方時）
 * @param paths 書き込み先
 * @param error 失敗したときの理由
 * @return 決められたら true
 */
bool prepareScreenshotPaths(const std::string& dir, const std::tm& when, ScreenshotPaths& paths, std::string& error);
