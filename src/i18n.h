// 画面（設定パネル・視界の表示）に出す文言の表（日本語・英語）。描画コードの中に文言を書かず、ここから引く。
// ログの文言は日本語のまま（ここには入れない）。
#pragma once

#include "config_types.h"

#include <string>

/**
 * Frame のシステム言語（設定ファイルに language が無いときの既定値）。
 * Steam の言語設定（~/.steam/registry.vdf の "language"、読むだけ）が日本語なら日本語、
 * 読めなければ LC_ALL / LC_MESSAGES / LANG を見て、どれでもなければ英語。結果は最初の 1 回だけ調べて覚えておく。
 * @return 言語
 */
Language systemLanguage();

/**
 * 設定ファイルに書く言語の名前。
 * @param language 言語
 * @return "ja" / "en"
 */
const char* languageCode(Language language);

/**
 * 設定ファイルの言語の名前を読む。
 * @param code "ja" / "en"
 * @param language 読めたときの書き込み先
 * @return 知っている名前なら true
 */
bool parseLanguage(const std::string& code, Language& language);

/**
 * 画面の文言一式。%s・%d は printf の書式。
 */
struct UiText {
    // ---- 見出し・状態 ----
    const char* title;            ///< aux ショートカット
    const char* statusRunning;    ///< 動作中
    const char* statusStandby;    ///< 待機中（本体は動いているが aux を置き換えていない）
    const char* statusHandedBack; ///< 従来の aux（コントローラーが無いので aux を SteamVR に返している）
    const char* statusStopped;    ///< 止まっています（本体が動いていない）
    const char* tabAssign;
    const char* tabBrightness;
    const char* tabPress;
    const char* tabCommands;
    const char* tabDisplay;
    const char* tabApp;
    const char* footerSaved;      ///< 押すとすぐ反映され、設定ファイルに保存されます。
    const char* footerBgOn;       ///< パネルを閉じても動き続ける旨
    const char* footerBgOff;      ///< パネルを閉じると元の動きに戻る旨
    const char* on;
    const char* off;
    // ---- アクションの名前 ----
    const char* actMic;
    const char* actBright;
    const char* actPass;
    const char* actShot;
    const char* actPerf;
    const char* actDash;
    const char* actRecenter;      ///< 視点のリセット
    const char* actOriginal;      ///< aux の元の動き
    const char* actMediaPlayPause;///< 再生・一時停止
    const char* actMediaNext;     ///< 次の曲
    const char* actMediaPrevious; ///< 前の曲
    const char* actCmd;
    const char* actNone;
    // ---- 割り当てタブ ----
    const char* recentFormat;     ///< さっき押した %s（%s は時刻）
    const char* recentTapFormat;  ///< さっきタップした %s（タップのカード。%s は時刻）
    const char* stMicMuted;
    const char* stMicOn;
    const char* stMicUnknown;
    const char* stBrightFormat;   ///< いま %s（%s）: 1 つ目は今の %、2 つ目は段の並び
    const char* stPassOn;
    const char* stPassOff;
    const char* stPassUnknown;
    const char* stPerfShown;
    const char* stPerfHidden;
    const char* stPerfStopped;
    const char* stPerfMissing;
    const char* stPerfUnknown;
    const char* stDash;
    const char* stOriginal;        ///< aux の元の動きのカードの 2 行目
    const char* stRecenter;        ///< 視点のリセットのカードの 2 行目（今向いている方を正面にします）
    const char* stMedia;           ///< 音楽の操作のカードの 2 行目（Chrome などで流している音楽）
    const char* stCmdMissing;      ///< 割り当てたコマンドがもう無い
    const char* missingCommand;    ///< 消えたコマンド（カードの名前）
    const char* stNone;
    const char* assignHeadingFormat;  ///< %s に割り当てる
    const char* assignHint;
    const char* tapNoteTitle;         ///< ヘッドセットのタップ（タップのカードの横の説明の見出し）
    const char* tapNote;              ///< かぶっている間だけ使える・横を軽くたたく
    const char* chipMissing;          ///< 入っていない
    const char* pickerActions;        ///< アクション（割り当ての列の切り替え）
    const char* pickerCommandsFormat; ///< コマンド（%s）（%s は件数）
    const char* pickerMedia;          ///< 音楽（割り当ての列の切り替え）
    const char* mediaNote;            ///< 音楽の側の説明（MPRIS に対応したアプリ・PC の音は対象外）
    const char* cmdAdd;               ///< ＋ コマンドを足す
    // ---- 明るさタブ ----
    const char* brightHeading;
    const char* brightWhereFormat;    ///< %s で、左から順に切り替わります
    const char* brightNowhere;
    const char* listSep;              ///< 押し方を並べるときの区切り（・）
    const char* brightStepFormat;     ///< %d 段目
    const char* brightNow;            ///< いま
    const char* brightRemove;
    const char* brightAdd;
    const char* brightLoop;
    const char* brightNote;
    // ---- 押し方タブ ----
    const char* pressHeading;
    const char* longTitle;
    const char* longHint;
    const char* gapTitle;
    const char* gapHint;
    const char* secondsFormat;        ///< %s 秒（%s は数字）
    const char* pressHold;            ///< 押し続ける（長押しの図の棒の中）
    const char* pressReleaseHere;     ///< ここで離せば 1 回押し
    const char* pressLongChipFormat;  ///< %s 秒で長押しに決まる
    const char* pressFirst;           ///< 1 回目（回数の区切りの図）
    const char* pressGapBox;          ///< この間に押すと 2 回目
    const char* pressGapChip;         ///< 押さなければ、ここで「1 回押し」に決まる
    const char* pressNote;
    // ---- コマンドタブ ----
    const char* cmdHeading;
    const char* cmdHint;
    const char* cmdUnused;            ///< まだ割り当てていません
    const char* cmdTested;            ///< 実行しました
    const char* cmdTestFailed;        ///< 実行できませんでした
    const char* cmdTest;              ///< 試す
    const char* cmdEdit;              ///< 編集
    const char* cmdDelete;            ///< 消す
    const char* cmdDeleteConfirm;     ///< もう一度押すと消す
    const char* cmdFolder;            ///< フォルダ（フォルダの実行ファイルの印）
    const char* cmdEmpty;
    const char* cmdNote;
    const char* sheetAddTitle;
    const char* sheetEditTitle;
    const char* fieldName;
    const char* fieldCommand;
    const char* namePlaceholder;
    const char* commandPlaceholder;
    const char* sheetHint;
    const char* sheetCancel;
    const char* sheetSave;
    const char* sheetQuoteError;      ///< " が閉じていない
    const char* keyboardName;         ///< SteamVR のキーボードの説明（名前）
    const char* keyboardCommand;      ///< SteamVR のキーボードの説明（コマンド）
    const char* keyboardIp;           ///< SteamVR のキーボードの説明（PC の IP アドレス）
    // ---- コマンドのプリセット（追加のシート） ----
    const char* sheetModePresets;     ///< プリセットから
    const char* sheetModeOwn;         ///< 自分で書く
    const char* presetVrcName;        ///< VRChat のミュート（PC）（登録するコマンドの名前にもなる）
    const char* presetVrcDesc;
    const char* presetSpkName;        ///< スピーカーのミュート（登録するコマンドの名前にもなる）
    const char* presetSpkDesc;
    const char* presetNote;           ///< 追加したあとは自分のコマンドと同じ
    const char* oscDestTitle;         ///< 送り先の PC
    const char* oscDestHint;
    const char* oscDestAuto;          ///< 自動
    const char* oscDestManual;        ///< 手で入れる
    const char* oscFoundFormat;       ///< 見つかりました: %s
    const char* oscFoundHint;
    const char* oscNotFound;          ///< つないでいる PC が見つかりません
    const char* oscNotFoundHint;
    const char* oscIpLabel;           ///< PC の IP アドレス
    const char* oscIpPlaceholder;
    const char* oscIpInvalid;         ///< IP アドレスの形ではありません
    const char* oscSetupTitle;        ///< PC の VRChat で、先に 1 回だけ
    const char* oscSetup1;
    const char* oscSetup2;
    const char* oscFirewall;          ///< 届かないとき・Frame のミュートとは別
    const char* spkDetailTitle;
    const char* spkDetail;
    const char* presetAdd;            ///< 追加する
    const char* oscTested;            ///< ［試す］で送れた
    const char* oscTestNoPc;          ///< ［試す］で PC が見つからない
    const char* oscTestFailed;        ///< ［試す］で送れない
    const char* stOscVoice;           ///< カードの 2 行目: PC の VRChat に送る
    const char* stOscFormat;          ///< カードの 2 行目: PC に OSC を送る（%s）
    // ---- 表示と音タブ ----
    const char* soundTitle;
    const char* soundHint;
    const char* hudTitle;
    const char* hudHint;
    const char* badgeTitle;
    const char* badgeHint;
    const char* previewCaption;
    const char* badgeText;            ///< マイク ミュート中（視界の上の表示）
    // ---- アプリタブ ----
    const char* bgTitle;
    const char* bgHintOn;
    const char* bgHintOff;
    const char* ptrTitle;          ///< カーソルのときは aux を元に戻す
    const char* ptrHintOn;
    const char* ptrHintOff;
    const char* checkTitle;
    const char* checkHint;
    const char* autoTitle;
    const char* autoHint;
    const char* autoNotInstalled;
    const char* autoBusy;
    const char* autoFailed;
    const char* langTitle;
    // ---- ほかのアプリ（vendor/frame-apps/strings.md より） ----
    const char* appsTitle;               ///< appsRelatedTitle
    const char* appsRelatedHint;
    const char* appsReasonPerf;          ///< frame-perf-overlay を使う理由
    const char* appsReasonKeyboard;      ///< frame-jp-keyboard を使う理由
    const char* appsAllTitle;
    const char* appsAllCountFormat;      ///< %1$d = 一覧の数、%2$d = 入っていない数
    const char* appsAllInstalledFormat;  ///< %d = 一覧の数
    const char* appsInstall;
    const char* chipRunning;             ///< appsChipRunning
    const char* appsChipInstalled;
    const char* appsChipBusy;
    const char* appsRelatedTag;
    const char* appsListSub;
    const char* appsListNote;
    const char* appsOpenInstaller;
    const char* appsConfirmTitleFormat;  ///< %s = アプリの名前
    const char* appsConfirmMenuTitle;
    const char* appsConfirmLead;
    const char* appsConfirmProgress;
    const char* appsConfirmMenu;
    const char* appsConfirmNoSudo;
    const char* appsConfirmClose;
    const char* appsConfirmCancel;
    const char* appsConfirmLaunch;
    const char* appsErrorBusy;
    const char* appsErrorNoDisplay;
    const char* appsErrorNoKonsole;
    const char* appsErrorFailed;
    const char* resetAll;
    const char* resetConfirm;
    const char* quitBoth;
    const char* quitApp;
    const char* quitConfirm;
    // ---- 更新（vendor/frame-updater/strings.md より） ----
    const char* updateVersionFormat;     ///< まだ確かめていないとき（%s は版）
    const char* updateUpToDateFormat;
    const char* updateChecking;
    const char* updateAvailableFormat;
    const char* updateButton;
    const char* updateManual;
    const char* updateConfirmFormat;
    const char* updateConfirmHint;
    const char* updateConfirmYes;
    const char* updateConfirmNo;
    const char* updateInstallingFormat;
    const char* updateInstalledFormat;
    const char* updateInstallFailed;
    const char* updateCheckFailed;
    const char* updateCheckNow;
    const char* updateRetry;
    const char* updateDismiss;
    const char* updateLogHint;
    // ---- 視界の表示（押したときのお知らせ） ----
    const char* hudMicMuted;
    const char* hudMicOn;
    const char* hudMicToggled;
    const char* hudBrightFormat;      ///< 明るさ %s%%
    const char* hudPassOn;
    const char* hudPassOff;
    const char* hudShot;
    const char* hudPerfShown;
    const char* hudPerfHidden;
    const char* hudPerfToggled;
    const char* hudDash;
    const char* hudCmd;
    const char* hudVrcVoice;          ///< VRChat にミュートの切り替えを送りました
    const char* hudOscFormat;         ///< OSC を送りました: %s
    const char* hudMediaPlaying;      ///< 再生（2 行目は曲名）
    const char* hudMediaPaused;       ///< 一時停止
    const char* hudMediaNext;         ///< 次の曲
    const char* hudMediaPrevious;     ///< 前の曲
    const char* hudRecenter;          ///< 視点をリセットしました
    const char* failMic;
    const char* failBright;
    const char* failPass;
    const char* failShot;
    const char* failPerf;
    const char* failDash;
    const char* failOriginal;         ///< aux の元の動きを送れません
    const char* failCmd;
    const char* failVrcVoice;         ///< VRChat に送れません
    const char* failOsc;              ///< OSC を送れません
    const char* failMediaPlayPause;   ///< 再生・一時停止できません
    const char* failMediaNext;        ///< 次の曲に進めません
    const char* failMediaPrevious;    ///< 前の曲に戻れません
    const char* failRecenter;         ///< 視点をリセットできません
    const char* reasonNoSteamVr;
    const char* reasonRoomViewApi;
    const char* reasonPerfMissing;
    const char* reasonPerfStopped;
    const char* reasonNoCommand;
    const char* reasonNoPc;           ///< つないでいる PC が見つかりません
    const char* reasonNoPlayer;       ///< 再生しているアプリが見つかりません
    const char* reasonMediaUnsupported;  ///< このアプリでは使えません
    const char* reasonNotWorn;        ///< ヘッドセットをかぶっている間だけ使えます（視点のリセット）
    const char* reasonNotTracking;    ///< ヘッドセットの位置が取れていません（視点のリセット）
    const char* reasonFailed;
    const char* subSep;               ///< 押し方と理由の間（・）
    // ---- 視界の表示（カーソルのときに aux を元に戻す・戻した） ----
    const char* hudHandBack;          ///< aux を元の動きに戻しました
    const char* hudHandBackSub;       ///< 頭のカーソルで操作する間
    const char* hudShortcutsBack;     ///< aux のショートカットに戻しました
};

/**
 * 言語の文言の表を返す。
 * @param language 言語
 * @return 文言の表（プログラムの終わりまで有効）
 */
const UiText& uiText(Language language);

/**
 * 押し方の名前（「2 回押し」「長押し」「2 回タップ」）。視界のお知らせ・割り当ての見出し・コマンドの割り当て先などで使う短い名前。
 * @param language 言語
 * @param slot 0〜4 が 1〜5 回押し、5 が長押し、6 が 2 回タップ、7 が 3 回タップ
 * @return 名前
 */
std::string slotLabel(Language language, int slot);

/**
 * 割り当てのカードに出す押し方の名前（タップは「ヘッドセットを 2 回タップ」。aux の押し方は slotLabel と同じ）。
 * @param language 言語
 * @param slot slotLabel と同じ
 * @return 名前
 */
std::string slotTitle(Language language, int slot);

/**
 * アクションの種類の名前（「マイクのミュート」など）。
 * @param language 言語
 * @param type 種類
 * @return 名前
 */
const char* actionLabel(Language language, ActionType type);

/**
 * printf の書式に文字列を 1 つ当てはめる。
 * @param format 書式（%s が 1 つ）
 * @param value 当てはめる文字列
 * @return 結果
 */
std::string formatText(const char* format, const std::string& value);

/**
 * 更新の手順（`UpdateStatus::step`）を文言にする。知らない手順はそのまま返す。
 * @param language 言語
 * @param step "start" / "download" / "verify" / "extract" / "install"
 * @return 文言
 */
std::string updateStepText(Language language, const std::string& step);

/**
 * 更新のエラーコード（`UpdateStatus::error`）を文言にする。知らないコードは「other」の文言にする。
 * @param language 言語
 * @param error frame-update.sh / update_check.h のエラーコード
 * @return 文言
 */
std::string updateErrorText(Language language, const std::string& error);
