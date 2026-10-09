// 設定パネルの OpenVR まわり（ダッシュボードのオーバーレイとサムネイル、ポインターの操作）。
// frame-perf-overlay の vr_overlay の、ダッシュボードの部分だけを持ってきたもの。SteamVR の設定は変えない。
#pragma once

#include "keyboard_text.h"
#include "vk_texture.h"

#include <cstdint>
#include <string>
#include <vector>

/** ダッシュボードの設定パネルへのポインター操作（座標は画像の左上が原点の px）。 */
struct PointerInput {
    enum class Type {
        Move,
        Down,
        Up,
        Leave,
        Scroll,  ///< コントローラーのスティックのなめらかなスクロール（y に縦の量。正で上）
    };
    Type type;
    double x = 0.0;
    double y = 0.0;
};

/** pollEvents() の結果。 */
struct DashboardEvents {
    bool quit = false;                  ///< SteamVR から終了を求められた（VREvent_Quit）
    bool closeRequested = false;        ///< ダッシュボードのアイコンの「閉じる」が押された（VREvent_OverlayClosed）
    std::vector<PointerInput> pointer;  ///< 設定パネルへの操作（届いた順）
    bool keyboardChanged = false;       ///< キーボードの入力で欄の文字が変わった（keyboardText を欄に入れる）
    bool keyboardFinished = false;      ///< キーボードが終わった（完了・閉じた）
    uint64_t keyboardField = 0;         ///< showKeyboard に渡した番号
    std::string keyboardText;           ///< 欄に入れる文字（keyboardChanged のとき）
    size_t keyboardCursor = 0;          ///< 入力位置（keyboardText の中のバイト位置）
};

/**
 * ダッシュボードの設定パネル（オーバーレイ型の OpenVR アプリ）。
 */
class DashboardOverlay {
public:
    /** connect() の結果。 */
    enum class ConnectResult {
        Ok,          ///< つながった
        NotRunning,  ///< SteamVR が起動していない（待って再試行する）
        Error,       ///< それ以外の失敗
    };

    DashboardOverlay() = default;
    ~DashboardOverlay();
    DashboardOverlay(const DashboardOverlay&) = delete;
    DashboardOverlay& operator=(const DashboardOverlay&) = delete;

    /**
     * SteamVR が起動していればオーバーレイ型でつなぎ、Vulkan とダッシュボードの設定パネルを用意する。
     * 起動していないときに SteamVR を立ち上げてしまわないよう、先に Background 型で有無を確かめる。
     * @param width 設定パネルの画像の幅（px）
     * @param height 設定パネルの画像の高さ（px）
     * @param message 失敗したときの理由
     * @return 接続結果
     */
    ConnectResult connect(int width, int height, std::string& message);

    /** 隠す → テクスチャを外す → オーバーレイを消す → 数フレーム待つ → VR_Shutdown → Vulkan を壊す。 */
    void shutdown();

    /**
     * たまったイベントを処理する。SteamVR の終了（VREvent_Quit）には AcknowledgeQuit_Exiting で応える。
     * @param includeSystem false なら設定パネルのイベントだけ見る
     * @return 終了要求と設定パネルへの操作
     */
    DashboardEvents pollEvents(bool includeSystem);

    /** @return 設定パネル（ダッシュボードのこのアプリのタブ）が今見えていれば true（IPC 3 回まで） */
    bool settingsVisible() const;

    /**
     * ダッシュボードを開いて、このアプリのタブを見せる（2 つ目の起動から知らせが来たとき）。
     */
    void showSettings();

    /**
     * 設定パネルのオーバーレイに SteamVR のキーボードを出す（IVROverlay::ShowKeyboardForOverlay）。
     * 結果は pollEvents() の keyboardDone / keyboardClosed で受け取る。
     * 開くたびに番号を変え（uUserValue に入れる）、前に開いたキーボードの遅れて届いたイベントでは閉じない。
     * @param field 呼び出し側の番号（結果に付いてくる。0〜255）
     * @param description キーボードに出す説明
     * @param existing 最初から入っている文字
     * @param cursor 開いた直後の入力位置（existing の中のバイト位置。std::string::npos なら末尾）
     * @return 出せたら true
     */
    bool showKeyboard(uint64_t field, const std::string& description, const std::string& existing,
                      size_t cursor = std::string::npos);

    /**
     * キーボードの方式を決める。true（既定）: KeyboardFlag_Minimal で開き、1 文字ずつ届く VREvent_KeyboardCharInput を
     * 自分で組み立てる。false: 今までの方式（Steam のキーボードが中で文字をため、完了で GetKeyboardText）。
     * @param minimal Minimal で開くなら true
     */
    void setKeyboardMinimal(bool minimal) { keyboardMinimal_ = minimal; }

    /**
     * KeyboardFlag_Modal を付けて開くかを決める。既定は付けない（付けるとパネルを押すたびに SteamVR がキーボードを閉じる）。
     * 付けないときは、シートを閉じる・パネルが隠れるなどで hideKeyboard() を呼んで自分で閉じる。
     * @param modal Modal を付けるなら true（環境変数 FRAME_AUX_KEYBOARD_MODAL=1 で今までの動きと比べるため）
     */
    void setKeyboardModal(bool modal) { keyboardModal_ = modal; }

    /** @return Minimal で開く方式なら true */
    bool keyboardMinimal() const { return keyboardMinimal_; }

    /** @return 開いているキーボードの欄の番号（showKeyboard に渡したもの） */
    uint64_t keyboardField() const { return keyboardField_; }

    /** @return 今の入力位置（Minimal のとき、ためた文字の中のバイト位置） */
    size_t keyboardCursor() const { return keyboardBuffer_.cursor(); }

    /**
     * 開いているキーボードの入力先を別の欄に切り替える（開き直さない。Minimal のときだけ）。
     * @param field 新しい欄の番号
     * @param existing その欄の今の文字
     * @param cursor 入力位置（existing の中のバイト位置。std::string::npos なら末尾）
     * @return 切り替えたら true（Minimal でない・開いていないときは false）
     */
    bool retargetKeyboard(uint64_t field, const std::string& existing, size_t cursor);

    /**
     * 開いているキーボードを閉じる（IVROverlay::HideKeyboard）。ためた文字は結果に入れて返すので、欄に確定してから閉じられる。
     * このあと届く、この回の Closed は使わない。
     * @param why ログに出す理由
     * @return keyboardChanged・keyboardFinished を立てた結果（開いていなければ空）
     */
    DashboardEvents hideKeyboard(const char* why);

    /**
     * 入力位置を移す（入力中の欄をレーザーで押したとき。キーボードは開いたまま）。Minimal のときだけ効く。
     * @param byteIndex 文字の中のバイト位置
     */
    void setKeyboardCursor(size_t byteIndex) { keyboardBuffer_.setCursor(byteIndex); }

    /** @return キーボードを開いている（結果を待っている）なら true */
    bool keyboardOpen() const { return keyboardOpen_; }

    /** @return 接続時に見つけた vrserver がまだ生きている（確かめられない）なら true */
    bool steamVrAlive() const;

    /**
     * ダッシュボードのサムネイル画像を送る（接続直後に 1 回）。
     * @param rgba 非乗算済み RGBA
     * @param size 一辺の px
     * @return 送れたら true
     */
    bool submitThumbnail(const uint8_t* rgba, int size);

    /**
     * 設定パネルの画像を送る。
     * @param rgba 非乗算済み RGBA 8bit（connect で指定した大きさ）
     * @return 送れたら true
     */
    bool submitSettings(const uint8_t* rgba);

private:
    bool connected_ = false;
    uint64_t dashboardHandle_ = 0;
    uint64_t thumbnailHandle_ = 0;
    int height_ = 0;
    int vrserverPid_ = -1;
    std::string lastError_;

    /**
     * キーボードを終える（欄に入れる文字を結果に入れる）。
     * @param result 結果を入れる先
     * @param text 欄に入れる文字
     * @param why ログに出す理由
     */
    void finishKeyboard(DashboardEvents& result, const std::string& text, const char* why);
    bool keyboardMinimal_ = true;      ///< KeyboardFlag_Minimal で開く
    bool keyboardModal_ = false;       ///< KeyboardFlag_Modal を付けて開く
    bool keyboardOpen_ = false;        ///< キーボードの結果を待っている
    uint64_t keyboardField_ = 0;       ///< 開いた欄の番号
    uint64_t keyboardSession_ = 0;     ///< 開いた回の番号（uUserValue の上の桁。前の回のイベントを見分ける）
    KeyboardText keyboardBuffer_;      ///< Minimal のとき、1 文字ずつためた文字列
    bool keyboardOverlayQueue_ = false;  ///< この回はオーバーレイのキューにキーボードのイベントが来た（全体のキューは使わない）
    std::string keyboardPolled_;       ///< 開いている間に GetKeyboardText で最後に読んだ文字
    double keyboardNextPoll_ = 0.0;    ///< 次に GetKeyboardText を読む時刻
    VulkanContext vulkan_;
    OverlayTexture settingsTexture_;
    OverlayTexture thumbnailTexture_;
};
