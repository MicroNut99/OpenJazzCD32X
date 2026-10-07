#!/bin/bash
# OpenJazz CD32X - fill music/ with the game's own 35 songs as WAV (J10b): the .PSM files of
# the data folder played by OpenJazz's libxmp. J11d (Micronut99): each song = one complete pass,
# faded over its last 2 s; the console repeats the CD track.
#   tools/make_music_wavs.sh [data folder]        (then ./build.sh puts them on the disc)
set -e
HERE="$(cd "$(dirname "$0")/.." && pwd)"
DATA="${1:-$HERE/JazzJackRabbitGame}"
MAXS="${MAX_SECONDS:-600}"   # J11d: safety limit only - each song is one complete pass
make -C "$HERE/host" -j"${JOBS:-1}" >/dev/null      # the libxmp objects come from the PC build
gcc -O2 -no-pie -I"$HERE/oj/ext/xmp" -o "$HERE/host/psm2wav" "$HERE/tools/psm2wav.c" "$HERE"/host/obj/xmp/*.o "$HERE"/host/obj/xmp/loaders/*.o -lm
mkdir -p "$HERE/music"
for line in $(grep -v '^#' "$HERE/tools/music_tracks.txt" | awk '{print $2}'); do
  name="${line%.}"
  src=$(ls "$DATA" | grep -i "^$name\.psm$" | head -1)
  [ -n "$src" ] || { echo "  note: $name.PSM not in $DATA - skipped"; continue; }
  "$HERE/host/psm2wav" "$DATA/$src" "$HERE/music/$name.wav" "$MAXS" 1   # one pass + 2 s fade
done
echo "==> $(ls "$HERE"/music/*.wav | wc -l) songs in music/ ($(du -sm "$HERE/music" | cut -f1) MB)"
