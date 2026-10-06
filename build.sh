#!/bin/bash
# OpenJazz CD32X - full build (J4a: boot test disc):
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
echo "==> engine";      make -C "$HERE/mars"
[ -f "$HERE/mars/OPENJAZZ.32X" ] || { echo "*** mars/OPENJAZZ.32X was not made - stopping"; exit 1; }
echo "==> ROM check";   python3 "$HERE/cd32x/ref/openlara/tools/prep_openlara_cd.py" "$HERE/mars/OPENJAZZ.32X" "$ROMS/OPENLARA.32X"
echo "==> SH2 loader";  cd "$HERE/cd32x/sh2"    && make -f Makefile.cdloader clean && make -f Makefile.cdloader
echo "==> Sub-CPU";     cd "$HERE/cd32x/subcpu" && make clean && make cd

ISO="$HERE/cd32x/subcpu/CDROMPlayer.iso"
[ -f "$ISO" ] || { echo "*** $ISO was not made - stopping"; exit 1; }
grep -a -q "$MARKER" "$ISO" || { echo "*** the disc does NOT contain the loader - stopping"; exit 1; }
for f in APP.BIN CDLOADER.BIN OPENLARA.32X IMAGE.RAW; do
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
