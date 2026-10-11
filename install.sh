#!/usr/bin/env bash
# Frame Aux Shortcuts を Steam Frame に入れる・更新する・外す。本体の上で、ふつうのユーザーで実行する（sudo 不要）。
#   ./install.sh                 入れる・更新する。自動起動（本体と設定パネル）を有効にし、SteamVR が動いていればその場で（再）起動する
#   ./install.sh --no-autostart  入れる・更新する。自動起動は無効にする（ダッシュボードの＋から起動して使う）
#   ./install.sh --uninstall     外す（設定ファイルとスクリーンショットは残す）
# リリースの tar.gz を展開したフォルダでも、ビルドしたリポジトリのフォルダ（build/ に実行ファイル）でも動く。
# frame-perf-overlay の install.sh と同じ形。/opt・/etc には何も書かない。
set -euo pipefail

name="frame-aux-shortcuts"
panel="frame-aux-shortcuts-panel"
here="$(cd "$(dirname "$0")" && pwd)"
config_home="${XDG_CONFIG_HOME:-$HOME/.config}"
data_home="${XDG_DATA_HOME:-$HOME/.local/share}"
state_home="${XDG_STATE_HOME:-$HOME/.local/state}"
bin_dir="$HOME/.local/bin"
unit_dir="$config_home/systemd/user"
units="$name.service $panel.service"
desktop="$data_home/applications/$name.desktop"
icons="$data_home/icons/hicolor"
icon_sizes="48 128 256"
config_dir="$config_home/$name"

usage() {
    cat <<'EOF'
Usage: ./install.sh [--no-autostart | --uninstall]
  (no option)     install or update, turn on autostart, and (re)start it if SteamVR is running
  --no-autostart  install or update with autostart off (start it from the + button in the dashboard)
  --uninstall     remove it (your settings in ~/.config/frame-aux-shortcuts/ and screenshots are kept)
EOF
}

uninstall=0
autostart=1
for arg in "$@"; do
    case "$arg" in
        --uninstall) uninstall=1 ;;
        --no-autostart) autostart=0 ;;
        -h|--help) usage; exit 0 ;;
        *) echo "Unknown option: $arg" >&2; usage >&2; exit 2 ;;
    esac
done

# ＋から手で起動したもの（サービスの外で動いているもの）があれば、SIGTERM で止める。
# 本体は SIGTERM でキー対応表を戻してから終わり、設定パネルはオーバーレイを消してから終わる。
# 引数: 実行ファイルの名前
stop_manual_instance() {
    local app="$1"
    local lock="${XDG_RUNTIME_DIR:-/run/user/$(id -u)}/$app.lock"
    [[ -r "$lock" ]] || return 0
    local pid
    pid="$(head -n 1 "$lock" 2>/dev/null | tr -dc '0-9')"
    [[ -n "$pid" ]] || return 0
    # ロックファイルの PID が古くて別のプロセスに使い回されていることがあるので、実行ファイルの名前を確かめる
    [[ -r "/proc/$pid/cmdline" ]] || return 0
    local argv0
    argv0="$( { tr '\0' '\n' < "/proc/$pid/cmdline"; } 2>/dev/null | head -n 1 || true)"
    [[ "$(basename -- "${argv0:-x}")" == "$app" ]] || return 0
    local service_pid
    service_pid="$(systemctl --user show -p MainPID --value "$app.service" 2>/dev/null || echo 0)"
    [[ "$pid" != "$service_pid" ]] || return 0
    echo "Stopping the running $app (PID $pid) that was started outside the service..."
    kill -TERM "$pid" 2>/dev/null || return 0
    for _ in $(seq 1 50); do
        [[ -d "/proc/$pid" ]] || return 0
        sleep 0.1
    done
    echo "Warning: PID $pid is still running." >&2
}

# .desktop とアイコンの一覧を作り直す（コマンドが無ければ何もしない）
refresh_caches() {
    if command -v update-desktop-database >/dev/null 2>&1 && [[ -d "$data_home/applications" ]]; then
        update-desktop-database "$data_home/applications" || true
    fi
    if command -v gtk-update-icon-cache >/dev/null 2>&1 && [[ -f "$icons/index.theme" ]]; then
        gtk-update-icon-cache -q "$icons" || true
    fi
    # Steam は GTK3 でアイコンを探し、icons フォルダ自体の更新時刻が変わったときだけ読み直す。
    # これが無いと、Steam を再起動するまで＋の一覧にアイコンが出ない
    touch "$data_home/icons" 2>/dev/null || true
}

if [[ "$uninstall" == 1 ]]; then
    # --now の停止は SIGTERM なので、本体はキー対応表を戻してから終わる
    # shellcheck disable=SC2086
    systemctl --user disable --now $units 2>/dev/null || true
    stop_manual_instance "$panel"
    stop_manual_instance "$name"
    for unit in $units; do rm -f "$unit_dir/$unit"; done
    rm -f "$bin_dir/$name" "$bin_dir/$panel" "$desktop" "$data_home/$name/frame-update.sh"
    rmdir "$data_home/$name" 2>/dev/null || true  # 空になったら消す
    # パネルが覚えていた最後のタブ（設定ではないので消す）
    rm -f "$state_home/$name/panel.json"
    rmdir "$state_home/$name" 2>/dev/null || true
    for size in $icon_sizes; do
        rm -f "$icons/${size}x${size}/apps/$name.png"
    done
    rm -f "$icons/scalable/apps/$name.svg"
    systemctl --user daemon-reload || true
    refresh_caches
    echo "Removed $name."
    if [[ -d "$config_dir" ]]; then
        echo "Your settings are kept in $config_dir. To delete them too: rm -r \"$config_dir\""
    fi
    exit 0
fi

if [[ "$(uname -m)" != "aarch64" ]]; then
    echo "This build is for the Steam Frame (aarch64), but this machine is $(uname -m)." >&2
    exit 1
fi

# 実行ファイル: tar.gz の中なら同じフォルダ、リポジトリなら build/
find_binary() {
    if [[ -f "$here/$1" ]]; then
        echo "$here/$1"
    elif [[ -f "$here/build/$1" ]]; then
        echo "$here/build/$1"
    else
        echo "Cannot find the $1 binary next to install.sh or in build/. Build it first (see README)." >&2
        exit 1
    fi
}
src_bin="$(find_binary "$name")"
src_panel="$(find_binary "$panel")"
for f in "contrib/$name.service" "contrib/$panel.service" "contrib/$name.desktop"; do
    if [[ ! -f "$here/$f" ]]; then
        echo "Missing $here/$f" >&2
        exit 1
    fi
done

# 動いている本体は、置き換える前に止めない（実行ファイルは一時ファイルに書いてから置き換えるので壊れない）。
# 新しい版で動かし直すのは、下の restart のとき
install -Dm755 "$src_bin" "$bin_dir/$name.new"
mv -f "$bin_dir/$name.new" "$bin_dir/$name"
install -Dm755 "$src_panel" "$bin_dir/$panel.new"
mv -f "$bin_dir/$panel.new" "$bin_dir/$panel"

# アイコン（Steam は hicolor の <サイズ>/apps からアイコンを探す。frame-perf-overlay と同じく 3 サイズ）
for size in $icon_sizes; do
    install -Dm644 "$here/contrib/icons/$name-$size.png" "$icons/${size}x${size}/apps/$name.png"
done
if [[ -f "$here/contrib/icons/$name.svg" ]]; then
    install -Dm644 "$here/contrib/icons/$name.svg" "$icons/scalable/apps/$name.svg"
fi

# .desktop（ダッシュボードの＋の一覧に出る。起動するのは設定パネル。パネルは本体が止まっていれば起動する）
mkdir -p "$(dirname "$desktop")"
sed "s|@BINARY@|$bin_dir/$panel|" "$here/contrib/$name.desktop" > "$desktop.tmp"
chmod 644 "$desktop.tmp"
mv -f "$desktop.tmp" "$desktop"
if command -v desktop-file-validate >/dev/null 2>&1; then
    desktop-file-validate "$desktop" || true
fi
refresh_caches

# systemd のユーザーサービス（本体と設定パネル）
for unit in $units; do
    install -Dm644 "$here/contrib/$unit" "$unit_dir/$unit"
done
systemctl --user daemon-reload

# 更新の仕組み（vendor/frame-updater/frame-update.sh を ~/.local/share/<name>/ に置く）
update_script="$here/vendor/frame-updater/frame-update.sh"
if [[ -f "$update_script" ]]; then
    install -Dm755 "$update_script" "$data_home/$name/frame-update.sh"
else
    echo "Warning: $update_script not found. Update from the dashboard won't work until you install a release that includes it." >&2
fi

# 次回の自動更新（frame-update.sh install）が使うオプションを書き残す（1 行 1 つ）
# 設定のフォルダは新しく作るとき 0700（設定ファイルにはコマンドの引数が入るので、ほかのユーザーから見せない）
(umask 077 && mkdir -p "$config_dir")
install_args="$config_dir/install-args"
: >"$install_args.new"
[[ "$autostart" == 0 ]] && echo "--no-autostart" >>"$install_args.new"
mv -f "$install_args.new" "$install_args"

steamvr_running=0
if systemctl --user is-active --quiet steamvr.service; then
    steamvr_running=1
fi

if [[ "$autostart" == 1 ]]; then
    # shellcheck disable=SC2086
    systemctl --user enable $units
    if [[ "$steamvr_running" == 1 ]]; then
        stop_manual_instance "$panel"
        stop_manual_instance "$name"
        # restart の停止も SIGTERM なので、古い本体はキー対応表を戻してから新しいものに替わる
        # shellcheck disable=SC2086
        systemctl --user restart $units
        sleep 2
        # shellcheck disable=SC2086
        systemctl --user --no-pager status $units | grep -E '●|Active:' || true
    fi
else
    # shellcheck disable=SC2086
    systemctl --user disable $units 2>/dev/null || true
    # サービスとして動いているものがあれば、新しい実行ファイルで起動し直す（次の SteamVR 起動からは自動では起動しない）
    for unit in $units; do
        if systemctl --user is-active --quiet "$unit"; then
            systemctl --user restart "$unit"
        fi
    done
fi

echo
echo "$name $("$bin_dir/$name" --version 2>/dev/null | awk '{print $2}') is installed."
if [[ "$autostart" == 1 ]]; then
    if [[ "$steamvr_running" == 1 ]]; then
        echo "  It is running now and starts together with SteamVR from now on."
    else
        echo "  It starts together with SteamVR from the next SteamVR start."
    fi
else
    echo "  Autostart is off. Start it from the + button in the SteamVR dashboard (Frame Aux Shortcuts)."
fi
cat <<EOF
  Settings: SteamVR dashboard > aux Shortcuts, or $config_dir/config.json
  Logs:     journalctl --user -u $name -u $panel -f
  If aux stops working after a crash: $bin_dir/$name --restore (or reboot)
  Remove:   ./install.sh --uninstall
EOF
