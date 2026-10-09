// 登録したコマンド（設定ファイルの commands）と、フォルダ（~/.config/frame-aux-shortcuts/commands/）の実行ファイル。
// コマンドはシェルを通さずに実行する（argv のまま）。文字列 ↔ argv の分け方もここ。
#pragma once

#include <string>
#include <vector>

/** コマンド 1 つ。 */
struct CommandDef {
    std::string id;                 ///< 設定ファイルのものは "c1" など、フォルダのものは "dir:<ファイル名>"
    std::string name;               ///< 表示する名前
    std::vector<std::string> argv;  ///< 実行するコマンドと引数
    bool fromFolder = false;        ///< フォルダの実行ファイル（編集・消すはできない）

    bool operator==(const CommandDef& o) const {
        return id == o.id && name == o.name && argv == o.argv && fromFolder == o.fromFolder;
    }
};

/** フォルダのコマンドの id の前置き。 */
constexpr const char* kFolderCommandPrefix = "dir:";

/**
 * コマンドの文字列を argv に分ける。空白（スペース・タブ）で区切り、"…" の中は 1 つにまとめる
 * （"…" の中では \" と \\ が使える）。引用符の外で ~/ から始まる引数は、ホームフォルダに広げる。
 * シェルの機能（変数・パイプ・リダイレクトなど）は無い。
 * @param text 文字列
 * @param error 読めなかったとき（"…" が閉じていない）の理由
 * @param argv 書き込み先
 * @return 読めたら true（空なら argv は空）
 */
bool splitCommandLine(const std::string& text, std::vector<std::string>& argv, std::string& error);

/**
 * argv を表示用・編集用の文字列にする（空白や " を含む引数は "…" で囲む）。splitCommandLine で戻せる。
 * @param argv コマンドと引数
 * @return 文字列
 */
std::string joinCommandLine(const std::vector<std::string>& argv);

/**
 * コマンドの中身（argv）をログに出してよいか（環境変数 FRAME_AUX_DEBUG=1 のときだけ）。
 * 引数に秘密の値を書くこともあるので、ふだんはジャーナルに名前と id だけを残す。
 * @return 出してよいなら true
 */
bool commandDebugEnabled();

/**
 * 割り当てたコマンドを起動した結果の説明（ログと状態ファイル用）。ふだんは名前と id だけで、argv は debug のときだけ足す。
 * @param name コマンドの名前
 * @param id コマンドの id
 * @param argv コマンドと引数
 * @param ok 起動できたら true
 * @param error 起動できなかった理由
 * @param debug argv も出すなら true
 * @return 説明
 * @example
 * commandLogDetail("通知", "c1", {"notify-send", "aux"}, true, "", false) // "通知（id c1、引数 2 個）を起動しました"
 */
std::string commandLogDetail(const std::string& name, const std::string& id, const std::vector<std::string>& argv,
                             bool ok, const std::string& error, bool debug);

/**
 * ［試す］の結果のログの 1 行。ふだんは label と引数の数だけで、argv は debug のときだけ足す。
 * @param label 何を試したか（コマンドの id や「編集中のコマンド」）
 * @param argv コマンドと引数
 * @param ok 起動できたら true
 * @param error 起動できなかった理由
 * @param debug argv も出すなら true
 * @return 改行つきの 1 行
 */
std::string tryCommandLogLine(const std::string& label, const std::vector<std::string>& argv, bool ok,
                              const std::string& error, bool debug);

/**
 * 設定ファイルのパスから、コマンドのフォルダのパスを決める（設定ファイルと同じフォルダの commands/）。
 * @param configPath 設定ファイルのパス
 * @return フォルダのパス
 */
std::string commandFolderFor(const std::string& configPath);

/**
 * フォルダの実行ファイル（実行権限のある通常のファイル。リンクは先をたどる）をコマンドにする。名前はファイル名
 * （最後の拡張子を除く）。隠しファイル（. で始まる）は除く。名前の順に並べる。フォルダが無ければ空。
 * @param folder フォルダのパス
 * @return コマンドの一覧
 */
std::vector<CommandDef> scanCommandFolder(const std::string& folder);

/**
 * id でコマンドを探す。
 * @param commands 設定のコマンド
 * @param folderCommands フォルダのコマンド
 * @param id id
 * @return 見つかったコマンド（無ければ nullptr）
 */
const CommandDef* findCommand(const std::vector<CommandDef>& commands, const std::vector<CommandDef>& folderCommands,
                              const std::string& id);

/**
 * 新しいコマンドの id（"c1"・"c2"… のうち、まだ使っていない一番小さいもの）。
 * @param commands 設定のコマンド
 * @return id
 */
std::string nextCommandId(const std::vector<CommandDef>& commands);
