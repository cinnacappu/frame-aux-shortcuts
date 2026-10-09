#!/usr/bin/env bash
# リリースの tar.gz を作る。Steam Frame の上で実行する（本体の cairo・Vulkan と SteamVR の OpenVR にリンクするため）。
#   scripts/package.sh   → dist/frame-aux-shortcuts-<バージョン>.tar.gz、dist/SHA256SUMS
# 中身（frame-aux-shortcuts/ の下）: 実行ファイル 2 つ（本体と設定パネル）、install.sh、
#       contrib（unit 2 つ・.desktop・アイコン・設定の例）、LICENSE・THIRD_PARTY_LICENSES.md・licenses/、
#       README・CHANGELOG、vendor/frame-updater/frame-update.sh（ダッシュボードからの更新に使う）。
# frame-perf-overlay の package.sh と同じ形。公開はしない（作って、公開のコマンドを表示するだけ）。
set -euo pipefail
cd "$(dirname "$0")/.."

name="frame-aux-shortcuts"
panel="$name-panel"
build_dir="build-release"

if [[ "$(uname -m)" != "aarch64" ]]; then
    echo "Run this on the Steam Frame (aarch64). This machine is $(uname -m)." >&2
    exit 1
fi

# vendor/frame-updater/・vendor/frame-apps/ が手で書き換えられていないか確かめる（元のリポジトリが隣にあればずれも見る）
sh vendor/frame-updater/verify.sh
sh vendor/frame-apps/verify.sh

# 開発用の build/ とは別のフォルダで、Release でビルドし直す（テストは開発用の build/ で ctest する。ここでは作らない）
cmake -G Ninja -S . -B "$build_dir" -DCMAKE_BUILD_TYPE=Release -DBUILD_TESTING=OFF
cmake --build "$build_dir" --clean-first

version="$("$build_dir/$name" --version | awk '{print $2}')"
if [[ -z "$version" || "$version" == "unknown" ]]; then
    echo "Could not read the version from $build_dir/$name --version" >&2
    exit 1
fi
panel_version="$("$build_dir/$panel" --version | awk '{print $2}')"
if [[ "$panel_version" != "$version" ]]; then
    echo "Version mismatch: $name says $version, $panel says $panel_version" >&2
    exit 1
fi

stage="$(mktemp -d)"
trap 'rm -rf "$stage"' EXIT
root="$stage/$name"
mkdir -p "$root/contrib/icons" "$root/vendor/frame-updater"
for bin in "$name" "$panel"; do
    install -m755 "$build_dir/$bin" "$root/$bin"
    strip "$root/$bin"
done
install -m755 install.sh "$root/"
install -m644 LICENSE THIRD_PARTY_LICENSES.md README.md README.ja.md CHANGELOG.md "$root/"
install -m644 "contrib/$name.service" "contrib/$panel.service" "contrib/$name.desktop" contrib/config.example.json \
    "$root/contrib/"
# PNG は install.sh が hicolor に入れ、設定パネルがダッシュボードのサムネイルにも使う。SVG は scalable に入る
install -m644 contrib/icons/*.png contrib/icons/*.svg "$root/contrib/icons/"
install -Dm644 licenses/LGPL-2.1.txt "$root/licenses/LGPL-2.1.txt"
# install.sh が ~/.local/share/<name>/ に置く更新スクリプト（cpp・python・strings.md・verify.sh は
# ビルド済みの実行ファイルに焼き込まれているか開発時にしか要らないので、リリースには入れない）
install -m755 vendor/frame-updater/frame-update.sh "$root/vendor/frame-updater/"

mkdir -p dist
out="dist/$name-$version.tar.gz"
# 持ち主の名前は入れない（uid 0 にそろえる）
tar -C "$stage" --owner=0 --group=0 --numeric-owner --sort=name -czf "$out" "$name"
echo
tar -tzvf "$out"

(cd dist && sha256sum "$(basename "$out")" >SHA256SUMS)
echo
cat "dist/SHA256SUMS"

echo
echo "To publish (this script does not do it). Put this version's CHANGELOG section in a file outside the repository first:"
echo "  gh release create v$version $out dist/SHA256SUMS --title v$version --notes-file <release-notes-file>"
