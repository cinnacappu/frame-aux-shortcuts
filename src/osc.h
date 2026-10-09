// つないでいる PC（Steam Link の相手）に OSC を送る組み込みのコマンド（`frame-aux-shortcuts --send-osc …`）。
// プリセット「VRChat のミュート（PC）」が使う。VRChat の /input/Voice に 1 を送り、少し待って 0 を送る（ボタンを押して離す）。
// OSC のパケットは自前で組む（int32 を 1 つだけ送る形）。送るのは UDP だけで、受け取りはしない。
#pragma once

#include <cstdint>
#include <string>
#include <vector>

/** 組み込みのコマンドの argv[0]（本体の実行ファイルの名前。PATH には頼らず、本体とパネルの中で動かす）。 */
constexpr const char* kSendOscProgram = "frame-aux-shortcuts";
/** 組み込みのコマンドの印（argv[1]）。 */
constexpr const char* kSendOscFlag = "--send-osc";
/** 送り先を自動で見つける印（送り先の書き方 "auto:9000" の前半）。 */
constexpr const char* kOscAutoHost = "auto";
/** VRChat が OSC を受け取るポート。 */
constexpr int kVrchatOscPort = 9000;
/** VRChat のマイクのミュートの切り替え（トグル式のとき、0 → 1 で切り替わる）。 */
constexpr const char* kVrchatVoiceAddress = "/input/Voice";
/** 組み込みのコマンドの結果の値の前置き（"osc:/input/Voice"。視界のお知らせの文言を選ぶのに使う）。 */
constexpr const char* kOscValuePrefix = "osc:";
/** 1 を送ってから 0 を送るまでの時間（ms。VRChat が 1 フレーム以上 1 を見るように）。 */
constexpr int kOscPressHoldMs = 150;

/** `--send-osc` の中身。 */
struct SendOscCommand {
    bool autoHost = true;  ///< 送り先を Steam Link の相手から見つける
    std::string host;      ///< 送り先の IPv4 アドレス（autoHost でないとき）
    int port = kVrchatOscPort;
    std::string address;   ///< OSC のアドレス（/ で始まる）
};

/**
 * OSC のメッセージ（int32 の引数を 1 つ）を組む。アドレスと型の文字列（",i"）は NUL で終わらせて 4 バイトの境目まで 0 で埋め、
 * 値はビッグエンディアン。
 * @param address OSC のアドレス（/ で始まる）
 * @param value 値
 * @return パケットのバイト列
 * @example
 * encodeOscInt("/input/Voice", 1).size() // 24（アドレス 16 ＋ ",i" 4 ＋ 値 4）
 */
std::string encodeOscInt(const std::string& address, int32_t value);

/**
 * IPv4 アドレスの書き方か（"192.168.0.12" の形。各数は 0〜255、先頭の 0 は付けない）。
 * @param text 文字列
 * @return 正しければ true
 */
bool isValidIpv4(const std::string& text);

/**
 * 送り先の書き方を読む（"auto:9000" か "<IPv4>:<ポート>"。ポートを省くと 9000）。
 * @param spec 書き方
 * @param out 読めたときの書き込み先（autoHost・host・port）
 * @return 読めたら true
 */
bool parseOscTarget(const std::string& spec, SendOscCommand& out);

/**
 * argv が組み込みのコマンド（argv[0] のファイル名が frame-aux-shortcuts、argv[1] が --send-osc、送り先、アドレスの 4 つ）か。
 * @param argv コマンドと引数
 * @param out そうなら中身の書き込み先
 * @return 組み込みのコマンドなら true
 */
bool parseSendOscCommand(const std::vector<std::string>& argv, SendOscCommand& out);

/**
 * 組み込みのコマンドの argv を作る（プリセットが登録する形）。
 * @param autoHost 送り先を自動で見つけるなら true
 * @param host 送り先の IPv4 アドレス（autoHost でないとき）
 * @param port ポート
 * @param address OSC のアドレス
 * @return argv
 */
std::vector<std::string> makeSendOscArgv(bool autoHost, const std::string& host, int port, const std::string& address);

/**
 * /proc/net/udp・/proc/net/udp6 の中身から、相手につながった（状態 01）ソケットのうち、inodes に含まれ、
 * 相手がループバックでも 0 でもない最初のものの相手の IP を読む。アドレスは 32 ビットごとに、そのマシンのバイト順の 16 進。
 * IPv4 を写した IPv6（::ffff:a.b.c.d）は IPv4 にする。
 * @param table ファイルの中身（1 行目は見出し）
 * @param inodes 調べるソケットの inode（空なら全部）
 * @param ip 見つかったときの書き込み先（"192.168.0.12" か IPv6 の文字列）
 * @return 見つかれば true
 */
/** OSC のアドレスの長さの上限（バイト）。 */
constexpr size_t kMaxOscAddress = 128;

bool findConnectedUdpPeer(const std::string& table, const std::vector<unsigned long long>& inodes, std::string& ip);

/**
 * 自動の送り先にしてよい私的な範囲のアドレスか。IPv4 は 10/8・172.16/12・192.168/16・169.254/16・100.64/10、
 * IPv6 は fc00::/7・fe80::/10（IPv4 を写した ::ffff:a.b.c.d は IPv4 として判定）。
 * @param ip アドレスの文字列
 * @return 私的な範囲なら true
 */
bool isLanAddress(const std::string& ip);

/**
 * いま Steam Link でつないでいる PC の IP を探す。Steam Link の受け手（実行ファイル名 vrlink）が持つソケットのうち、
 * 相手につながった UDP ソケットの相手（frameeyeosc の送り先の見つけ方と同じ考え方）。読むのは /proc だけ。
 * @param ip 見つかったときの書き込み先
 * @return 見つかれば true
 */
bool findSteamLinkPeer(std::string& ip);

/** sendOscPress() の結果。 */
struct SendOscResult {
    bool ok = false;
    std::string reason;  ///< 失敗の理由のコード: "no_pc"（つないでいる PC が見つからない）/ "failed"（送れない）
    std::string error;   ///< ログ用の説明（IP は入れない）
    std::string host;    ///< 送った先（ログには FRAME_AUX_DEBUG=1 のときだけ出す）
};

/**
 * 組み込みのコマンドを動かす: 送り先を決めて（自動なら探して）、アドレスに 1 を送り、holdMs 待って 0 を送る。
 * @param command 中身
 * @param holdMs 1 と 0 の間（ms）
 * @return 結果
 */
SendOscResult sendOscPress(const SendOscCommand& command, int holdMs = kOscPressHoldMs);
