#!/bin/sh
# 生成 Release 说明：只列支持的 kernel，功能说明在仓库首页 README。
# 用法: release_notes.sh <dist 目录>
set -eu

DIST="${1:-dist}"

echo 'Supported kernels'
echo ''
echo '| File | Android / kernel |'
echo '|---|---|'

for z in "$DIST"/droidspaces-lkm-*-arm64.zip; do
    [ -f "$z" ] || continue
    n=$(basename "$z")
    # droidspaces-lkm-android14-6.1-arm64.zip -> android14-6.1
    kmi=$(echo "$n" | sed -e 's/^droidspaces-lkm-//' -e 's/-arm64\.zip$//')
    # android14-6.1 -> 14 / 6.1
    and=$(echo "$kmi" | sed -e 's/^android//' -e 's/-.*$//')
    kver=$(echo "$kmi" | sed -e 's/^android[0-9]*-//')
    echo "| \`$n\` | Android $and / $kver |"
done

echo ''
echo '| File | Note |'
echo '|---|---|'
echo '| `droidspaces-lkm-arm64.zip` | All kernels in one package, picks the right one at boot |'
echo '| `source-*.tar.gz` | Source snapshot (GPL-2.0) |'