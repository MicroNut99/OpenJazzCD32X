#!/bin/bash
# OpenJazz CD32X - level packs (J5): for every LEVELn.www of the data folder, the PC build plays
# the level for 300 frames with the common pack mounted and records what the level needs that
# the common pack does not have: OJ<k>.PAK, k = www * 10 + n (port/pack.h). Music (*.PSM) and
# the demos stay out. Fails if a pack is bigger than the cart has room for (PACK_LEVEL_MAX).
#   tools/bake_levels.sh <data folder> <common pack> <output folder>
set -e
HERE="$(cd "$(dirname "$0")/.." && pwd)"
DATA="$1"; COMMON="$2"; OUT="$3"
MAX=$((0x170000))
make -C "$HERE/host" -j"${JOBS:-1}" >/dev/null
TMP=$(mktemp -d)
n=0; big=0; total=0
for f in $(ls "$DATA" | grep -i '^LEVEL[0-9]\.[0-9][0-9][0-9]$' | sort); do
  l=${f:5:1}; w=$((10#${f:7:3})); k=$(printf '%03d' $((w * 10 + l)))
  rm -f "$OUT/OJ$k.PAK"
  ( cd "$TMP" && T32X_PACK="$COMMON" T32X_BAKE="$OUT/OJ$k.PAK" T32X_BAKE_SKIP='*.PSM,MACRO.1,MACRO.2,MACRO.3,MACRO.4' \
    T32X_SAVE_EVERY=0 T32X_FRAMES_DIR="$TMP" T32X_MAX_FRAMES=400 T32X_MAX_SECONDS=100000 T32X_PAD="100:START,105:-" \
    "$HERE/host/openjazz32x_host" -w $w -l $l "$DATA" > "$TMP/log.txt" 2>&1 ) || true
  if [ ! -f "$OUT/OJ$k.PAK" ]; then echo "*** $f: no pack made"; tail -3 "$TMP/log.txt"; exit 1; fi
  s=$(stat -c %s "$OUT/OJ$k.PAK"); total=$((total + s)); n=$((n + 1))
  if [ $s -gt $MAX ]; then echo "*** $f -> OJ$k.PAK: $s bytes, more than $MAX"; big=1; fi
done
rm -rf "$TMP"
echo "    $n level packs, $((total / 1024)) KB in all"
[ $big = 0 ] || exit 1
