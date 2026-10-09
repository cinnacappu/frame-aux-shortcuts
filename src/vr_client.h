// SteamVR への接続（Background 型で有無を確かめてからオーバーレイ型でつなぐ。SteamVR を勝手に起動しない）と、
// 使う API の薄い包み。視界の表示のオーバーレイは hud_overlay.h。
#pragma once

#include <cstdint>
#include <map>
#include <string>

/**
 * SteamVR とのつながり（設定の読み書き・ダッシュボードを開く・終了の知らせ・room view のイベント）。
 */
class VrClient {
public:
    /** connect() の結果。 */
    enum class ConnectResult {
        Ok,          ///< つながった
        NotRunning,  ///< SteamVR が動いていない（起動はしない）
        Error,       ///< それ以外の失敗
    };

    VrClient() = default;
    VrClient(const VrClient&) = delete;
    VrClient& operator=(const VrClient&) = delete;
    ~VrClient();

    /**
     * SteamVR が動いていれば、オーバーレイ型でつなぐ（動いていなければ起動させずに NotRunning）。
     * @param message 失敗したときの理由（英語の説明）
     * @return 結果
     */
    ConnectResult connect(std::string& message);

    /** @return つながっていれば true */
    bool connected() const { return connected_; }

    /**
     * SteamVR のイベントを読み切る。終了の知らせ（VREvent_Quit）と room view の表示・非表示を覚える。
     * 終了の知らせを受けたことは SteamVR に返事する。
     */
    void pollEvents();

    /** @return 終了の知らせ（VREvent_Quit）が来ていれば true */
    bool quitRequested() const { return quitRequested_; }

    /**
     * イベント（VREvent_RoomViewShown / Hidden）で分かった room view の状態。
     * @param shown 分かっているときの書き込み先
     * @return イベントを一度でも受けていれば（または rememberRoomView() で覚えていれば）true
     */
    bool knownRoomView(bool& shown) const {
        shown = roomViewShown_;
        return roomViewKnown_;
    }

    /**
     * room view の状態を覚える（切り替えを頼んだときに、イベントが来る前の見込みとして入れる）。
     * @param shown 表示していれば true
     */
    void rememberRoomView(bool shown) {
        roomViewKnown_ = true;
        roomViewShown_ = shown;
    }

    /**
     * 今のシーンのアプリ（ゲーム）のプロセス ID（IVRApplications::GetCurrentSceneProcessId）。
     * @return プロセス ID。アプリが動いていない（ホーム画面）・つながっていなければ 0
     */
    uint32_t sceneProcessId();

    /**
     * 設定の float を読む（IVRSettings::GetFloat）。
     * @param section セクション（"steamvr" など）
     * @param key キー
     * @param value 読めたときの書き込み先
     * @param error 失敗したときの理由
     * @return 読めたら true
     */
    bool getFloat(const char* section, const char* key, float& value, std::string& error);

    /**
     * 設定の bool を読む（IVRSettings::GetBool）。
     * @param section セクション（"camera" など）
     * @param key キー
     * @param value 読めたときの書き込み先
     * @param error 失敗したときの理由
     * @return 読めたら true
     */
    bool getBool(const char* section, const char* key, bool& value, std::string& error);

    /**
     * 設定の float を書く（IVRSettings::SetFloat）。
     * @param section セクション
     * @param key キー
     * @param value 書く値
     * @param error 失敗したときの理由
     * @return 書けたら true
     */
    bool setFloat(const char* section, const char* key, float value, std::string& error);

    /**
     * ダッシュボードを開く（IVROverlay::ShowDashboard）。
     * @param error 失敗したときの理由
     * @return 頼めたら true（開いたかどうかまでは分からない）
     */
    bool showDashboard(std::string& error);

    /**
     * スクリーンショットを頼む（IVRScreenshots::RequestScreenshot、Mono）。vrcompositor が previewBase + ".png" に
     * VR の画面を書き、終わると VREvent_ScreenshotTaken / Failed が来る（pollEvents() で覚え、screenshotResult() で受け取る）。
     * @param previewBase 保存したい画像のファイル名（拡張子なし）
     * @param vrBase VR 用のファイル名（拡張子なし）
     * @param handle 頼めたときの番号の書き込み先
     * @param error 失敗したときの理由
     * @return 頼めたら true
     */
    bool requestScreenshot(const std::string& previewBase, const std::string& vrBase, uint32_t& handle,
                           std::string& error);

    /**
     * 撮れたスクリーンショットを Steam に渡す（IVRScreenshots::SubmitScreenshot。Steam が動いていれば、
     * ライブラリの今のアプリのスクリーンショットに入る）。
     * @param handle requestScreenshot() の番号
     * @param path 撮れた PNG（絶対パス、拡張子つき）
     * @param error 失敗したときの理由
     * @return 渡せたら true
     */
    bool submitScreenshot(uint32_t handle, const std::string& path, std::string& error);

    /**
     * 頼んだスクリーンショットの結果を受け取る（受け取ったら忘れる）。
     * @param handle requestScreenshot() の番号
     * @param ok 結果の書き込み先（撮れたら true）
     * @return 結果が来ていれば true（まだなら false）
     */
    bool screenshotResult(uint32_t handle, bool& ok);

    /** つながっていれば切る（VR_Shutdown）。 */
    void shutdown();

private:
    bool connected_ = false;
    bool quitRequested_ = false;
    bool roomViewKnown_ = false;  ///< room view の表示・非表示のイベントを受けたか
    bool roomViewShown_ = false;  ///< 最後に分かった room view の状態
    std::map<uint32_t, bool> screenshotResults_;  ///< スクリーンショットの番号 → 撮れたか（イベントで来たもの）
};
