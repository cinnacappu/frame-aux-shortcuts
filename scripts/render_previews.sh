#!/usr/bin/env bash
# 設定パネルと視界の表示を、日英・全タブ・主な状態で PNG に書き出す（見た目の確認用。OpenVR も実機の状態も使わない）。
# Frame の上で、ビルドしたフォルダで実行する:
#   scripts/render_previews.sh [build のフォルダ（既定 build）]
# 書き出し先: out/preview_YYYY-MM-DD_HH-MM-SS/
set -euo pipefail
cd "$(dirname "$0")/.."

build="${1:-build}"
panel="$build/frame-aux-shortcuts-panel"
daemon="$build/frame-aux-shortcuts"
out="out/preview_$(date '+%Y-%m-%d_%H-%M-%S')"
mkdir -p "$out"
# 自分の設定ファイルの影響を受けないよう、無いファイルを設定として渡す（既定値で描く）
config="$out/no-config.json"

shot() {
    local name="$1"
    shift
    "$panel" --config "$config" --dump-png "$out/$name.png" "$@" >/dev/null
}

for lang in ja en; do
    # 全タブ（ふつうの状態: 動作中・ミュート中・frame-perf-overlay 動作中・最新版）
    for tab in assign brightness press commands display app; do
        shot "panel-$lang-$tab" --language "$lang" --tab "$tab"
    done
    # 押し方: 秒数の端の値（長押し 0.3 / 1.5 秒、回数の区切り 0.2 / 0.8 秒）
    for pair in "300 200" "1500 800" "300 800" "1500 200" "1300 350" "1200 350"; do
        set -- $pair
        printf '{ "long_press_ms": %s, "multi_press_gap_ms": %s, "language": "%s" }\n' "$1" "$2" "$lang" > "$out/press.json"
        "$panel" --config "$out/press.json" --dump-png "$out/panel-$lang-press-long$1-gap$2.png" --tab press >/dev/null
    done
    rm -f "$out/press.json"
    # 割り当て: 下の列のコマンドの側（3 個で 1 ページ・10 個で 2 ページ）とアクションの側
    shot "panel-$lang-assign-cmd3" --language "$lang" --tab assign --preview-commands 3 --preview-assign-command 3 --slot 4
    shot "panel-$lang-assign-cmd10-page1" --language "$lang" --tab assign --preview-commands 10 --preview-assign-command 2 --slot 4 --preview-picker commands:0
    shot "panel-$lang-assign-cmd10-page2" --language "$lang" --tab assign --preview-commands 10 --preview-assign-command 2 --slot 4 --preview-picker commands:1
    shot "panel-$lang-assign-cmd10-actions" --language "$lang" --tab assign --preview-commands 10 --preview-assign-command 2 --slot 1
    shot "panel-$lang-assign-cmd-missing" --language "$lang" --tab assign --preview-commands 1 --preview-assign-command 7 --slot 4
    # コマンドのタブ: 一覧（上・一番下）・空・フォルダの分・消す確認・［試す］の結果・ドラッグ中・シート
    shot "panel-$lang-commands-top" --language "$lang" --tab commands --preview-commands 10 --preview-folder --preview-assign-command 2 --preview-tested c1
    shot "panel-$lang-commands-bottom" --language "$lang" --tab commands --preview-commands 11 --preview-folder --preview-scroll end --preview-tested dir:toggle-hotspot:fail
    shot "panel-$lang-commands-empty" --language "$lang" --tab commands --preview-commands 0
    shot "panel-$lang-commands-delete" --language "$lang" --tab commands --preview-commands 3 --preview-delete c2
    shot "panel-$lang-commands-drag" --language "$lang" --tab commands --preview-commands 10 --preview-drag c1:2
    shot "panel-$lang-commands-sheet-add" --language "$lang" --tab commands --preview-commands 3 --preview-sheet add
    shot "panel-$lang-commands-sheet-edit" --language "$lang" --tab commands --preview-commands 3 --preview-sheet tested
    shot "panel-$lang-commands-sheet-cannot-save" --language "$lang" --tab commands --preview-commands 3 --preview-sheet invalid
    shot "panel-$lang-commands-sheet-quote" --language "$lang" --tab commands --preview-commands 3 --preview-sheet quote
    shot "panel-$lang-commands-sheet-failed" --language "$lang" --tab commands --preview-commands 3 --preview-sheet failed
    # シートの入力中（空・文字あり・長い文字で末尾を見せている）。名前の欄とコマンドの欄
    shot "panel-$lang-commands-typing-name-empty" --language "$lang" --tab commands --preview-commands 3 --preview-sheet add --preview-typing "0:"
    shot "panel-$lang-commands-typing-name" --language "$lang" --tab commands --preview-commands 3 --preview-sheet add --preview-typing "0:スピーカーの Mute"
    shot "panel-$lang-commands-typing-name-long" --language "$lang" --tab commands --preview-commands 3 --preview-sheet add --preview-typing "0:とても長い名前のコマンドを入れて、欄の幅を超えたときに末尾側が見えるかを確かめるための名前 end"
    shot "panel-$lang-commands-typing-cmd-empty" --language "$lang" --tab commands --preview-commands 3 --preview-sheet edit --preview-typing "1:"
    shot "panel-$lang-commands-typing-cmd" --language "$lang" --tab commands --preview-commands 3 --preview-sheet edit --preview-typing "1:wpctl set-mute @DEFAULT_AUDIO_SINK@"
    shot "panel-$lang-commands-typing-cmd-long" --language "$lang" --tab commands --preview-commands 3 --preview-sheet edit --preview-typing "1:/home/steamos/bin/wifi-reconnect.sh --interface wlan0 --retry 3 --timeout 10 --verbose --end"
    # 入力位置が文字の途中（短い文字・長い文字の途中・長い文字の先頭の近く）
    shot "panel-$lang-commands-typing-name-mid" --language "$lang" --tab commands --preview-commands 3 --preview-sheet add --preview-typing "0:スピーカー|の Mute"
    shot "panel-$lang-commands-typing-name-long-mid" --language "$lang" --tab commands --preview-commands 3 --preview-sheet add --preview-typing "0:とても長い名前のコマンドを入れて、欄の幅を超えたときに|末尾側が見えるかを確かめるための名前 end"
    shot "panel-$lang-commands-typing-cmd-mid" --language "$lang" --tab commands --preview-commands 3 --preview-sheet edit --preview-typing "1:wpctl set-mute |@DEFAULT_AUDIO_SINK@ toggle"
    shot "panel-$lang-commands-typing-cmd-long-head" --language "$lang" --tab commands --preview-commands 3 --preview-sheet edit --preview-typing "1:/home/st|eamos/bin/wifi-reconnect.sh --interface wlan0 --retry 3 --timeout 10 --verbose --end"
    # 割り当て: 入っていないアプリ（frame-perf-overlay）を選んだカード、ミュートでない、本体が止まっている
    shot "panel-$lang-assign-perf-missing" --language "$lang" --tab assign --slot 4 --preview-perf missing
    shot "panel-$lang-assign-unmuted" --language "$lang" --tab assign --preview-unmuted --slot 3
    shot "panel-$lang-assign-daemon-stopped" --language "$lang" --tab assign --preview-daemon stopped
    # 割り当て: ヘッドセットのタップ（見本の割り当て・さっき 2 回タップ。2 回タップのカードを選ぶ・コマンドの側も）
    shot "panel-$lang-assign-taps" --language "$lang" --tab assign --preview-taps --slot 6
    shot "panel-$lang-assign-taps-tap3" --language "$lang" --tab assign --preview-taps --slot 7 --preview-commands 3
    # 割り当て: 音楽の操作（1 回押し・4 回押し・2 回タップに割り当てて、音楽の側。アクションの側も）
    shot "panel-$lang-assign-media" --language "$lang" --tab assign --preview-media --slot 0 --preview-commands 3
    shot "panel-$lang-assign-media-next" --language "$lang" --tab assign --preview-media --slot 3
    shot "panel-$lang-assign-media-actions" --language "$lang" --tab assign --preview-media --slot 0 --preview-picker actions
    shot "panel-$lang-assign-media-tap" --language "$lang" --tab assign --preview-media --slot 6 --preview-commands 12
    # 割り当て: 視点のリセット（長押し・2 回タップに割り当てて、長押しのカードを選ぶ）と、アクションの側の
    # 入っていない frame-perf-overlay（選んでいない・選んだ。5 列の幅で名前の下に札）
    shot "panel-$lang-assign-recenter" --language "$lang" --tab assign --preview-recenter --slot 5
    shot "panel-$lang-assign-recenter-perf-missing" --language "$lang" --tab assign --preview-recenter --slot 6 --preview-perf missing
    # アプリ: 更新の帯の状態（4 つの基本の状態＋確認・更新中・入れた・失敗）と、入っていない・止まっているアプリ
    for state in uptodate checking available checkfailed confirm installing installed installfailed manual; do
        shot "panel-$lang-app-update-$state" --language "$lang" --tab app --preview-update "$state"
    done
    shot "panel-$lang-app-perf-missing" --language "$lang" --tab app --preview-perf missing
    shot "panel-$lang-app-perf-stopped-bg-off" --language "$lang" --tab app --preview-perf stopped --preview-bg-off \
        --preview-autostart notinstalled
    shot "panel-$lang-app-quit-confirm" --language "$lang" --tab app --preview-quit
    shot "panel-$lang-app-reset-confirm" --language "$lang" --tab app --preview-reset --preview-autostart failed
done
"$daemon" --dump-hud-png "$out" >/dev/null
ls "$out" | wc -l | xargs echo "PNG:"
echo "$out"
