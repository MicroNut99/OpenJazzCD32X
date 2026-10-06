#!/bin/bash
# OpenJazz CD32X - full build (J4c: boot test + game data pack):
#   splash -> engine (mars/) -> ROM check (OpenLara's prep_openlara_cd.py) -> SH2 loader ->
#   Sub-CPU/68000 -> disc image, then the same disc checks OpenLara's build.sh makes.
# The loader (cd32x/sh2) and the Sub-CPU program (cd32x/subcpu) are OpenLara's, UNCHANGED;
# the engine goes on the disc under the name they open: OPENLARA.32X (file 20).
#   ./build.sh 2>&1 | tee build_log.txt
set -e
HERE="$(cd "$(dirname "$0")" && pwd)"
trap 'echo; echo "*** BUILD FAILED at line $LINENO: $BASH_COMMAND"; echo "*** look above for the first error (grep -n -B6 Error build_log.txt)"' ERR
MARKER="SH2-OPENLARA-CD-OL1-LOADER"
ROMS="$HERE/cd32x/subcpu/ROMS"
mkdir -p "$ROMS"

grep -q '^#define CDL_TARGET_OPENLARA 1' "$HERE/cd32x/sh2/cdloader_sh2.c" \
  || { echo "*** cd32x/sh2/cdloader_sh2.c is not in OpenLara mode - stopping"; exit 1; }

echo "==> splash";      python3 "$HERE/tools/make_boot_image.py" "$HERE/Splash.png" "$ROMS/IMAGE.RAW"
echo "==> data pack";   # J4c: the PC build plays boot -> intro -> main menu and records every
                        # file and picture it uses: IMAGE8.RAW (cart slot 8, see port/pack.h)
DATA="${DATA:-$HERE/JazzJackRabbitGame}"
[ -f "$DATA/PANEL.000" ] || { echo "*** no Jazz data in $DATA (set DATA=<folder>) - stopping"; exit 1; }
make -C "$HERE/host" -j"${JOBS:-1}" >/dev/null
BAKE_TMP="$(mktemp -d)"
( cd "$BAKE_TMP" && T32X_BAKE="$ROMS/IMAGE8.RAW" T32X_SAVE_EVERY=0 T32X_FRAMES_DIR="$BAKE_TMP" \
  T32X_MAX_FRAMES=940 "$HERE/host/openjazz32x_host" "$DATA" 2>&1 | grep "\[bake\]" )
# check: the same run from the pack alone (no data folder) must reach the menu
( cd "$BAKE_TMP" && T32X_PACK="$ROMS/IMAGE8.RAW" T32X_SAVE_EVERY=0 T32X_FRAMES_DIR="$BAKE_TMP" \
  T32X_MAX_FRAMES=940 "$HERE/host/openjazz32x_host" /nonexistent > "$BAKE_TMP/check.txt" 2>&1 ) || true
grep -q "reached 940 frames" "$BAKE_TMP/check.txt" || { echo "*** the pack alone does not reach the menu on the PC:"; tail -5 "$BAKE_TMP/check.txt"; exit 1; }
echo "    pack checked on the PC: $(grep -o 'peak [0-9]* bytes' "$BAKE_TMP/check.txt" | head -1) of heap (32X: ~207,000 free)"
rm -rf "$BAKE_TMP"
echo "==> engine";      make -C "$HERE/mars"
[ -f "$HERE/mars/OPENJAZZ.32X" ] || { echo "*** mars/OPENJAZZ.32X was not made - stopping"; exit 1; }
echo "==> ROM check";   python3 "$HERE/cd32x/ref/openlara/tools/prep_openlara_cd.py" "$HERE/mars/OPENJAZZ.32X" "$ROMS/OPENLARA.32X"
echo "==> SH2 loader";  cd "$HERE/cd32x/sh2"    && make -f Makefile.cdloader clean && make -f Makefile.cdloader
echo "==> Sub-CPU";     cd "$HERE/cd32x/subcpu" && make clean && make cd

ISO="$HERE/cd32x/subcpu/CDROMPlayer.iso"
[ -f "$ISO" ] || { echo "*** $ISO was not made - stopping"; exit 1; }
grep -a -q "$MARKER" "$ISO" || { echo "*** the disc does NOT contain the loader - stopping"; exit 1; }
for f in APP.BIN CDLOADER.BIN OPENLARA.32X IMAGE.RAW IMAGE8.RAW; do
  [ -f "$HERE/cd32x/subcpu/cd/$f" ] || { echo "*** $f is not on the disc - stopping"; exit 1; }
done
NAME="$(dd if="$HERE/cd32x/subcpu/cd/OPENLARA.32X" bs=1 skip=$((0x3C0)) count=8 2>/dev/null)"
[ "$NAME" = "OpenJazz" ] || { echo "*** cd/OPENLARA.32X is not the OpenJazz engine ($NAME) - stopping"; exit 1; }
cmp -s "$HERE/cd32x/subcpu/cd/OPENLARA.32X" "$ROMS/OPENLARA.32X" || { echo "*** the disc has an old engine - stopping"; exit 1; }
echo "==> disc checked: loader + OpenJazz engine + splash"

# a mixed-mode image (data + one 2 s silent audio track) for emulators that want a .cue
python3 - "$HERE/cd32x/subcpu/silence.wav" <<'PY'
import sys, wave
w = wave.open(sys.argv[1], "wb"); w.setnchannels(2); w.setsampwidth(2); w.setframerate(44100)
w.writeframes(b"\0" * 44100 * 4 * 2); w.close()
PY
cd "$HERE/cd32x/subcpu" && python3 make_mixed_cd2.py CDROMPlayer.iso silence.wav --out OpenJazzCD32X
echo
echo "==> done:"
echo "    emulator: cd32x/subcpu/OpenJazzCD32X.cue (with its .bin)"
echo "    burn:     cd32x/subcpu/OpenJazzCD32X.cue, or the plain cd32x/subcpu/CDROMPlayer.iso"
