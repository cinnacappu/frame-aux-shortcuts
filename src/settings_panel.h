// SteamVR ダッシュボードに出す設定パネル（描画・ボタンの当たり判定・操作の反映）。大きさは 1200×740、タブは 6 つ。
#pragma once

#include "autostart.h"
#include "commands.h"
#include "config.h"
#include "eye_actions.h"
#include "frame_apps.h"     // vendor/frame-apps/cpp（CMake の include dir で見つかる）
#include "status.h"
#include "update_check.h"  // vendor/frame-updater/cpp（CMake の include dir で見つかる）

#include <cstdint>
#include <string>
#include <utility>
#include <vector>

class FontSet;
struct Pen;
struct UiText;
typedef struct _cairo cairo_t;
typedef struct _cairo_surface cairo_surface_t;

/** タブ（見出しの右に、この順に並ぶ）。 */
enum class PanelTab { Assign, Brightness, Press, Commands, Display, App };
/** タブの数。 */
constexpr int kPanelTabCount = 6;

/**
 * タブの名前（--tab と、最後に選んだタブを覚えるファイルで使う）。
 * @param tab タブ
 * @return "assign" / "brightness" / "press" / "commands" / "display" / "app"
 */
const char* panelTabName(PanelTab tab);

/**
 * タブの名前を読む。
 * @param name 名前
 * @param tab 知っている名前なら書き込む
 * @return 知っている名前なら true
 */
bool parsePanelTab(const std::string& name, PanelTab& tab);

/** 追加のシートのプリセット（左の一覧の順）。 */
enum class CommandPreset { VrchatMute, SpeakerMute };
/** プリセットの数。 */
constexpr int kCommandPresetCount = 2;

/** 設定パネルのボタンが表す操作。arg は押し方の番号・アクションの種類・段の番号など。 */
struct PanelAction {
    enum class Kind {
        None,
        Tab,           ///< タブを選ぶ（arg = PanelTab）。パネルの中だけで扱う
        SelectSlot,    ///< 割り当てるカードを選ぶ（arg = 押し方の番号）。パネルの中だけで扱う
        Assign,        ///< 選んだカードにアクションを割り当てる（arg = ActionType、slot = 押し方の番号）
        LevelDec,      ///< 明るさの段を 5% 暗く（arg = 段）
        LevelInc,      ///< 明るさの段を 5% 明るく（arg = 段）
        LevelRemove,   ///< 段を消す（arg = 段）
        LevelAdd,      ///< 段を足す
        LongDec,
        LongInc,
        GapDec,
        GapInc,
        SoundOn,
        SoundOff,
        HudOn,
        HudOff,
        BadgeOn,
        BadgeOff,
        BackgroundOn,
        BackgroundOff,
        HeadPointerOn,     ///< コントローラーが接続されていないときは aux を従来の動きに戻す（original_aux_on_head_pointer）
        HeadPointerOff,
        CheckOn,
        CheckOff,
        LanguageJa,
        LanguageEn,
        AutostartOn,       ///< 呼び出し側で扱う（systemctl --user enable）
        AutostartOff,      ///< 呼び出し側で扱う（systemctl --user disable）
        ResetAll,          ///< すべて既定に戻す（2 回目の押下で確定したときだけ返る）
        Quit,              ///< 終了（2 回目の押下で確定したときだけ返る。呼び出し側で扱う）
        UpdateCheckNow,    ///< 呼び出し側で扱う
        UpdateInstall,     ///< 1 回目: 確認の表示に切り替えるだけ
        UpdateConfirmYes,  ///< 呼び出し側で扱う（インストールを始める）
        UpdateConfirmNo,   ///< 確認をやめる
        UpdateRetry,       ///< 呼び出し側で扱う
        UpdateDismiss,     ///< 呼び出し側で扱う
        PickerActions,     ///< 割り当ての列をアクションの側に（パネルの中だけで扱う）
        PickerMedia,       ///< 割り当ての列を音楽の側に（パネルの中だけで扱う）
        PickerEye,         ///< 割り当ての列を目の側に（パネルの中だけで扱う）
        PickerCommands,    ///< 割り当ての列をコマンドの側に（パネルの中だけで扱う）
        PickerPrev,        ///< コマンドの側の前のページ（パネルの中だけで扱う）
        PickerNext,        ///< コマンドの側の次のページ（パネルの中だけで扱う）
        AddCommand,        ///< 「＋ コマンドを足す」: コマンドのタブで追加のシートを出す（パネルの中だけで扱う）
        Grip,              ///< 一覧の行の左のつまみ（text = id。押したまま上下に動かすと並び替え。パネルの中で扱う）
        CommandTest,       ///< 一覧の［試す］（text = id。呼び出し側で実行して setRowTestResult）
        CommandEdit,       ///< 一覧の［編集］（text = id。パネルの中でシートを出す）
        CommandDelete,     ///< 一覧の［消す］（text = id。2 回目の押下で確定したときだけ返る。呼び出し側で消す）
        SheetName,         ///< シートの名前の欄（呼び出し側で SteamVR のキーボードを出す）
        SheetCommand,      ///< シートのコマンドの欄（呼び出し側で SteamVR のキーボードを出す）
        SheetTest,         ///< シートの［試す］（呼び出し側で実行して setSheetTestResult）
        SheetCancel,       ///< シートの［やめる］（パネルの中だけで扱う）
        SheetSave,         ///< シートの［保存する］（保存できるときだけ返る。呼び出し側で saveCommand して onCommandSaved）
        SheetModePresets,  ///< 追加のシートを「プリセットから」に（パネルの中だけで扱う）
        SheetModeOwn,      ///< 追加のシートを「自分で書く」に（パネルの中だけで扱う）
        PresetPick,        ///< プリセットを選ぶ（arg = CommandPreset。パネルの中だけで扱う）
        OscDestAuto,       ///< 送り先の PC を自動で見つける（パネルの中だけで扱う）
        OscDestManual,     ///< 送り先の PC の IP を手で入れる（パネルの中だけで扱う）
        SheetIp,           ///< IP アドレスの欄（呼び出し側で SteamVR のキーボードを出す）
        PresetTest,        ///< プリセットの［試す］（呼び出し側で presetCommand を実行して setPresetTestResult）
        PresetAdd,         ///< プリセットの［追加する］（追加できるときだけ返る。呼び出し側で presetCommand を登録して onCommandSaved）
        AppsOpenList,      ///< アプリタブの「ささけんの Frame アプリ」: 一覧を出す（パネルの中だけで扱う）
        AppsCloseList,     ///< 一覧の ✕（パネルの中だけで扱う）
        AppsListDrag,      ///< 一覧の地: 押したまま上下に動かすとスクロール（パネルの中だけで扱う）
        AppInstall,        ///< アプリの［入れる］（text = 呼び名）: 確認を出す（パネルの中だけで扱う）
        AppsOpenMenu,      ///< 一覧の［インストーラーを開く］: 確認を出す（パネルの中だけで扱う）
        AppsConfirmCancel, ///< 確認の［やめる］（パネルの中だけで扱う）
        AppsConfirmLaunch, ///< 確認の［Konsole で開く］（text = 呼び名、空ならメニュー。呼び出し側で openInstaller して setAppsLaunchResult）
    };
    Kind kind = Kind::None;
    int arg = 0;
    int slot = 0;
    std::string text;  ///< コマンドの id など

    PanelAction() = default;
    /**
     * @param kind 種類
     * @param arg 押し方の番号・アクションの種類・段の番号など
     * @param slot 押し方の番号（Assign）
     * @param text コマンドの id など
     */
    PanelAction(Kind kind, int arg = 0, int slot = 0, std::string text = std::string())
        : kind(kind), arg(arg), slot(slot), text(std::move(text)) {}

    bool operator==(const PanelAction& o) const {
        return kind == o.kind && arg == o.arg && slot == o.slot && text == o.text;
    }
    bool operator!=(const PanelAction& o) const { return !(*this == o); }
};

/** 設定のほかに、描くのに要るもの。 */
struct PanelView {
    /** 本体の状態。 */
    enum class Daemon {
        Running,  ///< 動いていて、aux を置き換えている
        Standby,  ///< 動いているが、aux は元の動き（パネルを閉じたら戻す設定で、パネルがまだ見えていない間など）
        HandedBack,  ///< 動いているが、コントローラーが無いので aux を SteamVR に返している
        Stopped,  ///< 動いていない
    };
    Daemon daemon = Daemon::Stopped;
    bool haveStatus = false;  ///< status が本体の今の状態か（動いていないときは false）
    Status status;
    AutostartStatus autostart;
    frame_updater::UpdateStatus update;
    long long now = 0;        ///< 今の時刻（Unix 時間の秒。「さっき押した」に使う）
    std::vector<CommandDef> folderCommands;  ///< コマンドのフォルダの実行ファイル
    std::vector<frame_apps::Entry> apps;     ///< ささけんのアプリ（使うものが先、自分は入っていない）
    bool menuBusy = false;                   ///< メニューの Konsole がまだ開いている
    EyePanelInfo eyePanel;                   ///< frameeyeosc-panel が入っているか・版（目のアクションが使えるか）
};

/**
 * 操作を設定に反映する（設定を変えない操作は何もしない）。
 * @param action 操作
 * @param config 書き換える設定
 * @return 設定が変わったら true
 */
bool applyPanelAction(const PanelAction& action, Config& config);

/**
 * 設定パネルの画像を描き、レーザーポインターの位置からボタンを判定する係。
 */
class SettingsPanel {
public:
    /** 割り当ての列の側（アクション・音楽・目・コマンド）。 */
    enum class PickerSide { Actions, Media, Eye, Commands };
    /** 画像の大きさ（px）。 */
    static constexpr int kWidth = 1200;
    static constexpr int kHeight = 740;
    /** 「終了」「すべて既定に戻す」「消す」を 1 回押してから、確定の 2 回目を待つ秒数。 */
    static constexpr double kConfirmSec = 3.0;
    /** 割り当ての列のコマンドの側の 1 ページの枠の数（4 列 × 2 行。最後のページの最後は「＋ コマンドを足す」）。 */
    static constexpr int kPickerPerPage = 8;
    /** コマンドのタブの一覧の行の高さと、行の間。 */
    static constexpr double kListRowH = 84;
    static constexpr double kListRowGap = 10;
    /** なめらかなスクロールの 1 の量で動かす一覧の量（px）。実機で合わせる。 */
    static constexpr double kScrollPxPerUnit = 120;
    /** シートの［試す］の結果。 */
    enum class TestResult { None, Ran, Failed };
    /** プリセットの［試す］の結果。 */
    enum class PresetTestResult {
        None,
        Ran,     ///< コマンドを起動した（スピーカーのミュート）
        Sent,    ///< OSC を送った
        NoPc,    ///< つないでいる PC が見つからない
        Failed,  ///< 起動できない・送れない
    };
    /** シートの欄の番号（SteamVR のキーボードの番号にもなる）。 */
    static constexpr int kFieldName = 0;
    static constexpr int kFieldCommand = 1;
    static constexpr int kFieldIp = 2;
    static constexpr int kFieldCount = 3;

    /**
     * @param fonts 使うフォント（このオブジェクトより長く生きていること）
     */
    explicit SettingsPanel(const FontSet& fonts);
    ~SettingsPanel();
    SettingsPanel(const SettingsPanel&) = delete;
    SettingsPanel& operator=(const SettingsPanel&) = delete;

    /**
     * 今の設定と状態で描く（ボタンの位置もここで決め直す）。
     * @param config 今の設定（言語もここから）
     * @param view 本体の状態・自動起動・更新
     */
    void render(const Config& config, const PanelView& view);

    /**
     * ポインターが動いた。
     * @param x 左端からの px
     * @param y 上端からの px
     * @return 乗っているボタンが変わった（描き直しが要る）なら true
     */
    bool pointerMove(double x, double y);

    /**
     * ボタンが押された。タブ・カードの選択・確認の表示はパネルの中で扱い、None を返す。
     * @param x 左端からの px
     * @param y 上端からの px
     * @param now 現在時刻（秒、単調増加）
     * @return 呼び出し側で扱う操作（無ければ None）
     */
    PanelAction pointerDown(double x, double y, double now);

    /**
     * ボタンが離された。
     * @return 描き直しが要るなら true
     */
    bool pointerUp();

    /**
     * ポインターがパネルから外れた。
     * @return 描き直しが要るなら true
     */
    bool pointerLeave();

    /**
     * コントローラーのスティックのスクロール（ダッシュボードのオーバーレイの VREvent_ScrollSmooth）。
     * コマンドのタブの一覧を縦にスクロールし、割り当てタブのコマンドの側はページをめくる（1 ぶんたまるごとに 1 ページ）。
     * @param dy 縦のスクロール（正で上）
     * @return 見た目が変わった（描き直しが要る）なら true
     */
    bool scroll(double dy);

    /**
     * つまみで並び替えている間に、一覧の上下の端の近くなら自動でスクロールする（メインループから細かく呼ぶ）。
     * @param dt 前の呼び出しからの秒数
     * @return 見た目が変わったら true
     */
    bool dragTick(double dt);

    /** @return つまみで並び替えている最中か */
    bool dragging() const { return !dragId_.empty(); }

    /**
     * つまみを離したときの新しい並びを受け取る（受け取ったら忘れる）。
     * @param ids 並び（id）の書き込み先
     * @return 並び替えが終わったばかりなら true
     */
    bool takeReorder(std::vector<std::string>& ids);

    /** @return 追加・編集のシートを出しているか */
    bool sheetOpen() const { return sheetOpen_; }
    /** @return シートで編集しているコマンドの id（追加なら空） */
    const std::string& sheetId() const { return sheetId_; }
    /** @return シートの名前の欄 */
    const std::string& sheetName() const { return sheetName_; }
    /** @return シートのコマンドの欄 */
    const std::string& sheetCommand() const { return sheetCommand_; }

    /**
     * シートの欄の文字。
     * @param field kFieldName / kFieldCommand / kFieldIp
     * @return 文字（知らない番号なら空）
     */
    const std::string& sheetFieldText(int field) const;

    /**
     * シートの欄が今見えているか（見えない欄に入力しているキーボードは閉じる）。
     * @param field kFieldName / kFieldCommand / kFieldIp
     * @return 見えていれば true
     */
    bool sheetFieldShown(int field) const;

    /**
     * シートの欄に文字を入れる（SteamVR のキーボードの結果）。
     * @param field kFieldName（0）/ kFieldCommand（1）/ kFieldIp（2）
     * @param text 文字
     * @param cursor 入力位置（text の中のバイト位置。std::string::npos なら末尾）
     */
    void setSheetText(int field, const std::string& text, size_t cursor = std::string::npos);

    /** @return 追加のシートが「プリセットから」か（編集のシートや閉じているときは false） */
    bool presetMode() const { return sheetOpen_ && sheetId_.empty() && presetMode_; }
    /** @return 選んでいるプリセット */
    CommandPreset preset() const { return preset_; }
    /**
     * 見た目の確認用・テスト用: 追加のシートの側とプリセット・送り先を決める。
     * @param presets 「プリセットから」なら true
     * @param preset プリセット
     * @param autoDest 送り先を自動にするなら true
     */
    void setPresetForPreview(bool presets, CommandPreset preset, bool autoDest) {
        presetMode_ = presets;
        preset_ = preset;
        oscAuto_ = autoDest;
    }

    /**
     * 選んでいるプリセットのコマンド（登録する名前と argv）。
     * @param language 名前の言語
     * @param name 名前の書き込み先
     * @param argv argv の書き込み先
     * @return 追加・実行できる形なら true（手で入れる IP が正しくないときは false）
     */
    bool presetCommand(Language language, std::string& name, std::vector<std::string>& argv) const;

    /** @return 送り先の PC を探して見せる必要があるか（「プリセットから」で VRChat を選び、送り先が自動） */
    bool wantsPeer() const { return presetMode() && preset_ == CommandPreset::VrchatMute && oscAuto_; }

    /**
     * 見つけた送り先の PC を見せる。
     * @param found 見つかったら true
     * @param ip 見つかった IP
     * @return 見た目が変わったら true
     */
    bool setDetectedPeer(bool found, const std::string& ip);

    /**
     * プリセットの［試す］の結果を出す。
     * @param result 結果
     */
    void setPresetTestResult(PresetTestResult result) { presetTest_ = result; }

    /**
     * 欄を押したときに、押した位置に一番近い文字の境目を受け取る（受け取ったら忘れる）。入力中でも、閉じていても求める。
     * 文字が空のときや、文字の右の空きを押したときは末尾。
     * @param byteIndex 入力位置の書き込み先
     * @return 押した位置があれば true
     */
    bool takeTapCursor(size_t& byteIndex);

    /** @return 入力中の欄の入力位置（バイト位置） */
    size_t sheetCursor() const { return sheetCursor_; }

    /**
     * シートの［試す］の結果を出す。
     * @param ran 実行できたら true
     */
    void setSheetTestResult(bool ran);

    /**
     * 一覧の［試す］の結果を出す。
     * @param id コマンドの id
     * @param ran 実行できたら true
     */
    void setRowTestResult(const std::string& id, bool ran);

    /**
     * シートを出す。追加なら「プリセットから」で始める（VRChat のミュート・送り先は自動）。
     * @param command 編集するコマンド（nullptr なら追加）
     */
    void openSheet(const CommandDef* command);

    /** シートを閉じる（保存せずに）。 */
    void closeSheet() {
        sheetOpen_ = false;
        activeField_ = -1;
    }

    /** カーソルの点滅の間隔（秒）。 */
    static constexpr double kCaretBlinkSec = 0.53;

    /**
     * キーボードで入力している欄を決める（枠をピンクにして、カーソルを出す）。
     * @param field kFieldName / kFieldCommand / kFieldIp、-1 = どれでもない（キーボードが閉じた）
     * @param cursor 入力位置（その欄の文字の中のバイト位置。std::string::npos なら末尾）
     */
    void setActiveField(int field, size_t cursor = std::string::npos) {
        if (field != activeField_) fieldScroll_ = 0.0;  // 同じ欄を開き直したときは、横のずらしをそのまま続ける
        activeField_ = field;
        caretRestart_ = true;
        if (field >= 0 && field < kFieldCount) {
            const std::string& value = sheetFieldText(field);
            sheetCursor_ = cursor == std::string::npos || cursor > value.size() ? value.size() : cursor;
        }
    }

    /** @return 入力している欄（-1 = どれでもない） */
    int activeField() const { return activeField_; }

    /**
     * カーソルの点滅を進める（キーボードを開いている間だけ呼ぶ）。文字が入ったあとは点灯から始め直す。
     * @param now 今の時刻（秒、単調増加）
     * @return 点いた・消えた（描き直しが要る）なら true
     */
    bool caretTick(double now);

    /**
     * 保存できたときに呼ぶ（シートを閉じ、足したコマンドのページへ移る）。
     * @param id 保存したコマンドの id
     * @param config 保存した後の設定
     */
    void onCommandSaved(const std::string& id, const Config& config);

    /** 見た目の確認用: コマンドの「消す」の 2 回目を待つ状態にする。 */
    void armDeleteForPreview(const std::string& id);
    /** 見た目の確認用: 割り当ての列の側とページを決める（自動で決めない）。 */
    void setPickerForPreview(PickerSide side, int page);
    /** 見た目の確認用: コマンドのタブの一覧のスクロール（px。大きければ一番下）。 */
    void setListScrollForPreview(double px) { listScroll_ = px; }
    /**
     * 見た目の確認用: つまみで並び替えている途中の見た目にする（最初に描いた並びから rows 行ぶん動かした状態）。
     * @param id 動かしているコマンド
     * @param rows 動かした行の数（正で下）
     */
    void dragForPreview(const std::string& id, int rows);

    /**
     * 時間で変わる表示（確認の期限切れ）を進める。
     * @param now 現在時刻（秒、単調増加）
     * @return 描き直しが要るなら true
     */
    bool tick(double now);

    /**
     * 描いた画像を OpenVR 用の非乗算済み RGBA にして返す。
     * @return kWidth * kHeight * 4 バイト
     */
    const std::vector<uint8_t>& toRgba();

    /**
     * 描いた画像を PNG で保存する。
     * @param path 保存先
     * @return 保存できたら true
     */
    bool writePng(const std::string& path) const;

    /** @return 今のタブ */
    PanelTab tab() const { return tab_; }
    /** @param tab 見せるタブ */
    void setTab(PanelTab tab) { tab_ = tab; }
    /** @return 割り当てタブで選んでいる押し方 */
    int selectedSlot() const { return selectedSlot_; }
    /** @param slot 割り当てタブで選ぶ押し方（列の側は割り当てに合わせて決め直す） */
    void setSelectedSlot(int slot) {
        selectedSlot_ = slot;
        autoPicker_ = true;
    }

    /** @return アプリの一覧か確認を出しているか */
    bool appsListOpen() const { return appsListOpen_; }
    /** @return アプリの状態を見ている（アプリタブか、一覧・確認を出している） */
    bool appsShown() const { return tab_ == PanelTab::App; }
    /**
     * Konsole を開いた結果を出す（開けたら確認を閉じる）。
     * @param result openInstaller の結果
     */
    void setAppsLaunchResult(frame_apps::LaunchResult result);
    /**
     * 見た目の確認用: アプリの一覧・確認を出す。
     * @param list 一覧を出す
     * @param confirmKey 確認を出すアプリの呼び名（"menu" ならメニュー、空なら確認は出さない）
     * @param scroll 一覧のスクロール（px。大きければ一番下）
     */
    void openAppsForPreview(bool list, const std::string& confirmKey, double scroll);

    /**
     * 確認の画面が見せているアプリ（今の一覧 view.apps から呼び名で引く。同梱の一覧は使わない）。
     * @param view 今の一覧
     * @return アプリ（メニューのとき・一覧に無いときは nullptr）
     */
    const frame_apps::Entry* appsConfirmEntry(const PanelView& view) const;

    /**
     * 確認の画面に出すコマンド。Konsole に渡すもの（AppsManager::openInstaller → installerCommand）と同じ関数で作る。
     * @param view 今の一覧
     * @return コマンド（一覧に無い呼び名ならメニューのコマンド。そのときは［Konsole で開く］を押せない）
     */
    std::string appsConfirmCommand(const PanelView& view) const;

    /** 見た目の確認用: 「もう一度押すと終了」の状態にする。 */
    void armQuitForPreview();
    /** 見た目の確認用: 「もう一度押すと既定に戻す」の状態にする。 */
    void armResetForPreview();
    /** 見た目の確認用: 更新の確認（「%s に更新しますか？」）の状態にする。 */
    void armUpdateConfirmForPreview();

    /**
     * テスト用: 操作のボタンの真ん中の座標（最後の render() の配置）。
     * @param action 操作
     * @param x 書き込み先
     * @param y 書き込み先
     * @return ボタンがあれば true
     */
    bool buttonCenter(const PanelAction& action, double& x, double& y) const;

    /**
     * テスト用: 操作のボタンの位置と大きさ（最後の render() の配置）。
     * @param action 操作
     * @param x 左の書き込み先
     * @param y 上の書き込み先
     * @param w 幅の書き込み先
     * @param h 高さの書き込み先
     * @return ボタンがあれば true
     */
    bool buttonBounds(const PanelAction& action, double& x, double& y, double& w, double& h) const;

private:
    /** ボタン 1 個。 */
    struct Button {
        PanelAction action;
        double x, y, w, h;
        bool enabled;
    };

    const FontSet& fonts_;
    cairo_surface_t* surface_ = nullptr;
    cairo_t* cr_ = nullptr;
    std::vector<uint8_t> rgba_;
    std::vector<Button> buttons_;
    PanelTab tab_ = PanelTab::Assign;
    int selectedSlot_ = 0;
    PanelAction hover_;
    PanelAction pressed_;
    bool quitArmed_ = false;
    double quitArmedUntil_ = 0.0;
    bool resetArmed_ = false;
    double resetArmedUntil_ = 0.0;
    bool updateConfirmArmed_ = false;
    PickerSide pickerSide_ = PickerSide::Actions;  ///< 割り当ての列の側
    int pickerPage_ = 0;              ///< コマンドの側のページ
    bool autoPicker_ = true;          ///< 次に描くとき、選んだカードの割り当てに合わせて側とページを決める
    int pickerPageCount_ = 1;         ///< 最後に描いたときのコマンドの側のページ数
    double pickerScrollAccum_ = 0.0;  ///< コマンドの側のページをめくるためにたまったスクロール
    double listScroll_ = 0.0;         ///< コマンドのタブの一覧のスクロール（px）
    double listMaxScroll_ = 0.0;      ///< 最後に描いたときのスクロールの上限
    double listBoxTop_ = 0.0;         ///< 最後に描いたときの一覧の箱の上端
    double listBoxBottom_ = 0.0;      ///< 一覧の箱の下端
    bool listScrollToEnd_ = false;    ///< 次に描くとき、一番下までスクロールする（足したとき）
    std::string dragId_;              ///< つまみで動かしているコマンド（空なら動かしていない）
    std::vector<std::string> dragOrder_;     ///< 動かしている間の並び
    std::vector<std::string> dragOrigOrder_; ///< 動かし始めたときの並び
    int dragOrigIndex_ = 0;
    double dragStartY_ = 0.0;         ///< 押したときのポインターの y
    double dragStartScroll_ = 0.0;    ///< 押したときのスクロール
    double dragPointerY_ = 0.0;       ///< 今のポインターの y
    bool reorderDone_ = false;        ///< 離した並びをまだ渡していない
    std::vector<CommandDef> lastCommands_;  ///< 最後に描いたときのコマンド（設定 ＋ フォルダ）
    bool sheetOpen_ = false;
    std::string sheetId_;
    std::string sheetName_;
    std::string sheetCommand_;
    TestResult sheetTest_ = TestResult::None;
    int activeField_ = -1;            ///< キーボードで入力している欄（-1 = どれでもない）
    size_t sheetCursor_ = 0;          ///< 入力中の欄の入力位置（バイト位置）
    double fieldScroll_ = 0.0;        ///< 入力中の欄の横のずらし（px。カーソルが見えるように）
    bool tapCursorPending_ = false;   ///< 入力中の欄を押した位置を、まだ渡していない
    size_t tapCursor_ = 0;
    double fieldTextX_[kFieldCount] = {0.0, 0.0, 0.0};  ///< 最後に描いた欄の文字の左端（ずらす前）
    bool presetMode_ = true;          ///< 追加のシートが「プリセットから」か
    CommandPreset preset_ = CommandPreset::VrchatMute;
    bool oscAuto_ = true;             ///< VRChat の送り先を自動で見つける
    std::string oscIp_;               ///< 手で入れた送り先の IP
    bool peerFound_ = false;          ///< 自動で送り先の PC が見つかった
    std::string peerIp_;              ///< 見つかった IP
    PresetTestResult presetTest_ = PresetTestResult::None;
    bool caretOn_ = true;             ///< カーソルが点いている
    bool caretRestart_ = true;        ///< 次の caretTick で点灯から始め直す
    double caretSince_ = 0.0;         ///< 点灯から始めた時刻
    std::string testedRowId_;         ///< 一覧で最後に［試す］を押したコマンド
    bool testedRowRan_ = false;
    std::string deleteArmedId_;       ///< 「消す」の 2 回目を待っているコマンド
    double deleteArmedUntil_ = 0.0;
    bool appsListOpen_ = false;       ///< アプリの一覧を出している
    bool appsConfirmOpen_ = false;    ///< Konsole で開く前の確認を出している
    std::string appsConfirmKey_;      ///< 確認しているアプリの呼び名（空ならメニュー）
    bool appsConfirmFromList_ = false;  ///< 確認を一覧から開いた（やめたら一覧に戻る）
    frame_apps::LaunchResult appsLaunchResult_ = frame_apps::LaunchResult::Started;  ///< 最後に開けなかった理由（Started なら無し）
    double appsScroll_ = 0.0;         ///< 一覧のスクロール（px）
    double appsMaxScroll_ = 0.0;      ///< 最後に描いたときのスクロールの上限
    bool appsDragging_ = false;       ///< 一覧の地を押したまま動かしている
    double appsDragStartY_ = 0.0;
    double appsDragStartScroll_ = 0.0;

    /**
     * ボタンを足す（描くたびに置き直す）。
     * @param action 操作
     * @param x 左
     * @param y 上
     * @param w 幅
     * @param h 高さ
     * @param enabled 押せるか
     */
    void addButton(const PanelAction& action, double x, double y, double w, double h, bool enabled = true);

    /**
     * 座標にある押せるボタンを探す。
     * @param x 左端からの px
     * @param y 上端からの px
     * @return 操作。無ければ None
     */
    PanelAction hitTest(double x, double y) const;

    /** @return 操作のボタンにポインターが乗っているか押しているか */
    bool isHot(const PanelAction& action) const { return hover_ == action || pressed_ == action; }

    /**
     * 2 択の切り替え（丸い枠の中に 2 つのボタン、選んでいる方はピンクの塗り ＋ ✓）を描く。
     * @param pen 描画の道具
     * @param x 左（alignRight なら右端）
     * @param y 上
     * @param left 左の操作
     * @param right 右の操作
     * @param leftLabel 左の文言
     * @param rightLabel 右の文言
     * @param selected 選んでいる方（0 = 左、1 = 右、-1 = どちらでもない）
     * @param enabled 押せるか
     * @param alignRight x を右端として置くか
     * @return 幅（px）
     */
    double drawToggle(const Pen& pen, double x, double y, const PanelAction& left, const PanelAction& right,
                      const std::string& leftLabel, const std::string& rightLabel, int selected, bool enabled,
                      bool alignRight);

    /** 見出し（題名・状態のピル・タブ）。 */
    void drawHeader(const Pen& pen, const UiText& t, const Config& config, const PanelView& view);
    /** 割り当てタブ。 */
    void drawAssignTab(const Pen& pen, const UiText& t, const Config& config, const PanelView& view);
    /** 明るさタブ。 */
    void drawBrightnessTab(const Pen& pen, const UiText& t, const Config& config, const PanelView& view);
    /** 押し方タブ。 */
    void drawPressTab(const Pen& pen, const UiText& t, const Config& config, const PanelView& view);
    /** 割り当てタブの下の列（アクション・コマンドの切り替えとページ）。 */
    void drawPicker(const Pen& pen, const UiText& t, const Config& config, const PanelView& view, double secY);
    /** コマンドタブ。 */
    void drawCommandsTab(const Pen& pen, const UiText& t, const Config& config);
    /** 追加・編集のシート。 */
    void drawSheet(const Pen& pen, const UiText& t, Language lang, double secX, double secY, double secW, double secH);
    /**
     * 文字の欄（高さ 56。押すと SteamVR のキーボード。入力中はピンクの枠とカーソル）。
     * @param pen 描画の道具
     * @param field 欄の番号
     * @param x 左
     * @param y 上
     * @param w 幅
     * @param placeholder 空のときの例の文字
     */
    void drawTextField(const Pen& pen, int field, double x, double y, double w, const std::string& placeholder);
    /**
     * 名前とコマンドの欄・説明（自分で書く・編集）。
     * @return 説明の下の y
     */
    double drawSheetFields(const Pen& pen, const UiText& t, double ix, double y, double innerW);
    /** 自分で書く・編集のボタンの行（［試す］結果・［やめる］［保存する］）。 */
    void drawSheetButtons(const Pen& pen, const UiText& t, double ix, double y, double innerW);
    /** プリセットの一覧と、選んだプリセットの中身。 */
    void drawPresetBody(const Pen& pen, const UiText& t, Language lang, double ix, double y, double innerW, double h);
    /** プリセットのボタンの行（［試す］結果・［やめる］［追加する］）。 */
    void drawPresetButtons(const Pen& pen, const UiText& t, Language lang, double ix, double y, double innerW);
    /** 表示と音タブ。 */
    void drawDisplayTab(const Pen& pen, const UiText& t, const Config& config);
    /** アプリタブ。 */
    void drawAppTab(const Pen& pen, const UiText& t, const Config& config, const PanelView& view);
    /**
     * アプリの 1 行（札・名前・説明か理由・状態の札か［入れる］）。
     * @param showReason 説明の代わりに、このアプリで使う理由（ピンク）を出す
     * @param inList 一覧の中（見えている部分だけ押せる）
     * @param clipTop 押せる範囲の上端（inList のとき）
     * @param clipBottom 押せる範囲の下端
     */
    void drawAppRow(const Pen& pen, const UiText& t, Language lang, const frame_apps::Entry& entry, double x, double y,
                    double w, double h, bool showReason, bool inList, double clipTop, double clipBottom);
    /** ささけんのアプリの一覧のシート。 */
    void drawAppsList(const Pen& pen, const UiText& t, Language lang, const PanelView& view);
    /** Konsole で開く前の確認のシート。 */
    void drawAppsConfirm(const Pen& pen, const UiText& t, const PanelView& view);
    /**
     * アプリタブの上の、新しい版の確認・インストールの帯。
     * @return 帯の下端の y
     */
    double drawUpdateBox(const Pen& pen, const UiText& t, const Config& config, const PanelView& view);
};

/**
 * ダッシュボードの下に並ぶサムネイル（アイコン）を描く。言語によらず同じ。
 * @param fonts 使うフォント
 * @param size 一辺の px
 * @param rgba 非乗算済み RGBA の書き込み先
 * @param pngPath 空でなければ PNG にも保存する
 */
void renderThumbnail(const FontSet& fonts, int size, std::vector<uint8_t>& rgba, const std::string& pngPath = "");
