// 実行時のファイルの置き場所（$XDG_RUNTIME_DIR）と、ほかのプロセスの確かめ方（ロックファイルの PID と /proc）。
// 読むだけ。ほかのプロセスのロックは取らない（取ると、そのプロセスの起動とぶつかるため）。
#pragma once

#include <sys/types.h>

#include <string>

/** 本体の実行ファイルの名前。 */
constexpr const char* kDaemonName = "frame-aux-shortcuts";
/** 設定パネルの実行ファイルの名前。 */
constexpr const char* kPanelName = "frame-aux-shortcuts-panel";
/** パフォーマンス表示のアプリの名前。 */
constexpr const char* kPerfOverlayName = "frame-perf-overlay";

/**
 * 実行時のファイルを置くフォルダ（$XDG_RUNTIME_DIR。無ければ /run/user/<uid>、それも無ければ /tmp）。
 * Steam から・systemd から・SSH から起動しても同じ場所になるようにする。
 * @return フォルダのパス
 */
std::string runtimeDir();

/**
 * このアプリ（本体・パネル）の実行時のフォルダ。$XDG_RUNTIME_DIR（か /run/user/<uid>）なら <そこ>/frame-aux-shortcuts、
 * どちらも使えないときの予備は /tmp/frame-aux-shortcuts-<uid>（状態ファイルもロックもこの中）。
 * @return フォルダのパス
 */
std::string appRuntimeDir();

/**
 * アプリのロックファイルのパス。$XDG_RUNTIME_DIR などでは <runtimeDir>/<名前>.lock（frame-perf-overlay と同じ決め方）。
 * /tmp の予備のときは、このアプリのものは appRuntimeDir() の中、ほかのアプリ（読むだけ）は /tmp/<名前>-<uid>.lock。
 * @param app アプリの名前
 * @return パス
 */
std::string lockFilePath(const std::string& app);

/**
 * 本体が書く状態ファイルのパス（<appRuntimeDir>/status.json）。
 * @return パス
 */
std::string statusFilePath();

/**
 * 自分だけのフォルダかを確かめる（lstat で、ディレクトリ（シンボリックリンクでない）・持ち主が uid・グループと他人に権限が無い）。
 * @param dir フォルダ
 * @param uid 持ち主であるべきユーザー ID
 * @param reason 満たさないときの理由
 * @return 満たせば true
 */
bool checkPrivateDir(const std::string& dir, uid_t uid, std::string& reason);

/**
 * フォルダを 0700 で作り（元からあってもよい）、自分だけのフォルダかを確かめる（checkPrivateDir）。
 * ほかのユーザーが先に作ったフォルダやシンボリックリンクには書かないため。
 * @param dir フォルダ
 * @param reason 使えないときの理由
 * @return 使えるなら true
 */
bool ensurePrivateDir(const std::string& dir, std::string& reason);

/**
 * ファイルを O_RDWR | O_CREAT | O_NOFOLLOW | O_CLOEXEC、権限 0600 で開き、ふつうのファイルで持ち主が自分かを確かめる（ロック用）。
 * @param path パス
 * @param reason 開けないときの理由
 * @return ファイル記述子（開けなければ -1）
 */
int openPrivateFile(const std::string& path, std::string& reason);

/**
 * このアプリのロックファイルを開く（/tmp の予備のときは、先にフォルダが自分だけのものかを確かめる）。
 * @param app kDaemonName か kPanelName
 * @param reason 開けないときの理由
 * @return ファイル記述子（開けなければ -1）
 */
int openLockFile(const std::string& app, std::string& reason);

/**
 * ファイルを全部読む。
 * @param path パス
 * @param text 書き込み先
 * @return 読めたら true
 */
bool readTextFile(const std::string& path, std::string& text);

/**
 * /proc/<pid>/cmdline の中身（NUL 区切り）の argv[0] のファイル名が name か。
 * @param cmdline NUL 区切りのコマンドライン
 * @param name 実行ファイルの名前
 * @return 同じなら true
 */
bool isCmdlineOf(const std::string& cmdline, const std::string& name);

/**
 * PID のプロセスが生きていて、その実行ファイルの名前が name か（PID の使い回しで別のプロセスを見誤らない）。
 * @param pid プロセス ID
 * @param name 実行ファイルの名前
 * @return そうなら true
 */
bool processIs(pid_t pid, const std::string& name);

/**
 * アプリのロックファイルに書いてある PID で、そのアプリが動いているかを確かめる（ロックは取らない）。
 * @param app アプリの名前
 * @return 動いていればその PID、動いていなければ 0
 */
pid_t runningPid(const std::string& app);

/**
 * 実行ファイルを探す（~/.local/bin、次に PATH。systemd や Steam から起動したときは PATH に ~/.local/bin が無いため）。
 * PATH の相対のフォルダ（'/' で始まらないもの）は飛ばす。
 * @param name 実行ファイルの名前
 * @return 見つかったパス（無ければ空）
 */
std::string findExecutable(const std::string& name);

/**
 * 実行ファイルが入っているか（findExecutable で見つかるか）。
 * @param name 実行ファイルの名前
 * @return 見つかれば true
 */
bool isInstalled(const std::string& name);

/**
 * 自分の cgroup が、そのユニット（systemd のサービス）か。
 * @param unit ユニットの名前（"frame-aux-shortcuts.service"）
 * @return そのサービスとして動いていれば true
 */
bool runningAsUnit(const std::string& unit);

/**
 * 実行時のファイルを書く: フォルダを 0700 で作って自分だけのものかを確かめ（ensurePrivateDir）、writeFilePrivately で置き換える。
 * フォルダが確かめられなければ書かない。
 * @param path 書くファイル
 * @param text 中身
 * @param error 書けなかったときの理由
 * @return 書けたら true
 */
bool writeFileAtomically(const std::string& path, const std::string& text, std::string& error);

/**
 * 一時ファイル（同じフォルダ、O_WRONLY | O_CREAT | O_EXCL | O_NOFOLLOW | O_CLOEXEC、権限 0600）に書いて fsync してから
 * rename で置き換える（読む側が書きかけを読まない。置き換わったファイルは 0600 になる）。フォルダは作らない。
 * @param path 書くファイル
 * @param text 中身
 * @param error 書けなかったときの理由
 * @return 書けたら true
 */
bool writeFilePrivately(const std::string& path, const std::string& text, std::string& error);

/**
 * `wpctl get-volume` の出力からミュートかを読む（"Volume: 1.00 [MUTED]" の形）。
 * @param text 出力
 * @param muted 読めたときの書き込み先
 * @return 読めたら true
 */
bool parseWpctlMuted(const std::string& text, bool& muted);

/**
 * frame-perf-overlay の設定ファイル（~/.config/frame-perf-overlay/config.json）の visible を読む（読むだけ）。
 * @return 1 / 0、読めなければ -1
 */
int readPerfOverlayVisible();
