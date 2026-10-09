// 設定パネルの見た目の状態（最後に選んだタブ）を覚えておくファイル。
// 設定ファイル（本体が見張っている）とは分けて、$XDG_STATE_HOME（無ければ ~/.local/state）の
// frame-aux-shortcuts/panel.json に置く。パネルが落ちても残るよう、変わったらすぐ書く。
#pragma once

#include <string>

/** 状態のファイルを読む大きさの上限（バイト）。 */
constexpr long kMaxPanelStateBytes = 4096;

/**
 * 状態ファイルのパス（$XDG_STATE_HOME/frame-aux-shortcuts/panel.json、無ければ ~/.local/state/…）。
 * @return パス（XDG_STATE_HOME も、/ で始まる HOME も無ければ空。そのときはタブを覚えない）
 */
std::string panelStatePath();

/**
 * 覚えておいたタブの名前を読む。ファイルがシンボリックリンクなら読まない。
 * @param path ファイルのパス
 * @param tab 読めたときの書き込み先（"assign" など。知っている名前かは呼び出し側で確かめる）
 * @return 読めたら true
 */
bool loadPanelTab(const std::string& path, std::string& tab);

/**
 * タブの名前を書く（フォルダを 0700 で作って自分だけのものかを確かめ、一時ファイル → rename で置き換える。0600）。
 * 親のフォルダ（~/.local/state など）が無ければ 0700 で作る。
 * @param path ファイルのパス
 * @param tab タブの名前（英数字と _ だけ）
 * @param error 書けなかったときの理由
 * @return 書けたら true
 */
bool savePanelTab(const std::string& path, const std::string& tab, std::string& error);
