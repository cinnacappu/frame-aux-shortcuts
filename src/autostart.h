// 自動起動（systemd ユーザーサービス）の状態の読み取りと、有効・無効の切り替え。frame-perf-overlay の autostart.h を
// 2 つのユニット（本体 frame-aux-shortcuts.service と設定パネル frame-aux-shortcuts-panel.service）用にしたもの。
// 読むのはユニットとリンクの有無だけ。切り替えは systemctl --user enable / disable を
// シェルを通さずに子プロセスで実行し、終わるのを待たない（UI を固めない）。
#pragma once

#include <string>
#include <sys/types.h>

/**
 * 設定パネルに出すための、自動起動の今の状態。
 */
struct AutostartStatus {
    enum class State {
        NotInstalled,  ///< 本体のユニットのファイルが無い（ボタンはグレー）
        Enabled,       ///< 本体のユニットのリンクが steamvr.service.wants にある
        Disabled,      ///< ユニットはあるがリンクが無い
    };
    State state = State::NotInstalled;
    bool busy = false;    ///< systemctl を実行中
    bool failed = false;  ///< 直前の切り替えが失敗した
};

/**
 * 自動起動の状態を読み、systemctl --user enable / disable を非同期で実行する係。
 */
class Autostart {
public:
    Autostart();
    ~Autostart();
    Autostart(const Autostart&) = delete;
    Autostart& operator=(const Autostart&) = delete;

    /**
     * ユニットとリンクの有無を読み直す（stat 2 回だけなので軽い）。
     * @return 状態が変わったら true
     */
    bool refresh();

    /**
     * 有効・無効の切り替えを始める（入っているユニットをまとめて。本体と設定パネル）。
     * 実行中・本体のユニットが無い・すでにその状態のときは何もしない。
     * @param enable true で enable、false で disable
     * @return 子プロセスを始めたら true
     */
    bool request(bool enable);

    /**
     * 実行中の子プロセスが終わっていれば回収し（waitpid の WNOHANG）、結果を反映する。
     * @return 状態が変わった（描き直しが要る）なら true
     */
    bool poll();

    /** @return 今の状態 */
    const AutostartStatus& status() const { return status_; }

    /**
     * 本体のユニットを起動する（systemctl --user start。終わるのは待たない）。設定パネルを開いたのに本体が
     * 止まっているときに使う。ユニットが無ければ何もしない。
     * @return 起動を頼んだら true
     */
    bool startDaemon();

    /** 本体のユニットの名前。 */
    static constexpr const char* kDaemonUnit = "frame-aux-shortcuts.service";
    /** 設定パネルのユニットの名前。 */
    static constexpr const char* kPanelUnit = "frame-aux-shortcuts-panel.service";

private:
    std::string unitDir_;       ///< $XDG_CONFIG_HOME/systemd/user
    std::string unitPath_;      ///< 本体のユニット
    std::string linkPath_;      ///< 本体のユニットの steamvr.service.wants のリンク
    std::string systemctl_;     ///< systemctl の絶対パス（子では PATH を探さない）
    AutostartStatus status_;
    pid_t child_ = -1;
    int errorPipe_ = -1;        ///< 子の標準エラーを受け取る（失敗の理由をログに残す）
    std::string childOutput_;
    bool requestedEnable_ = false;

    /**
     * 子の出力を、たまっている分だけ読む（ブロックしない）。
     */
    void drainPipe();
};
