#!/bin/bash
# SuperTux CD32X - build.
# R1a (this round): SuperTux 0.1.3's data in 32X form + previews + the music as CD tracks.
# The engine, the disc and the first playable level follow in R1b (with Hurrican's boot path,
# cd32x/ and port/, which are already here unchanged).
#   ./build.sh 2>&1 | tee build_log.txt
# SuperTux 0.1.3 is read from SuperTux-0.1.3/ next to this file (tools/stx_fetch.sh gets it)
# or from SUPERTUX=<folder>.
set -e
HERE="$(cd "$(dirname "$0")" && pwd)"
trap 'echo; echo "*** BUILD FAILED at line $LINENO: $BASH_COMMAND"; echo "*** look above for the first error (grep -n -B6 rror build_log.txt)"' ERR
SRC="${SUPERTUX:-$HERE/SuperTux-0.1.3}"
OUT="$HERE/build"

echo "==> boot path check (must be Hurrican CD32X's, unchanged; diagnostics on)"
python3 "$HERE/tools/check_boot.py"
python3 -c "import numpy, PIL" 2>/dev/null \
  || { echo "*** Python needs numpy and Pillow: sudo apt install python3-numpy python3-pil - stopping"; exit 1; }
[ -f "$SRC/data/images/tilesets/supertux.stgt" ] || "$HERE/tools/stx_fetch.sh" "$SRC"

echo "==> data (tiles, palette, backgrounds, levels) and previews"
rm -rf "$OUT/data" "$OUT/previews"
python3 "$HERE/tools/stx_convert.py" "$SRC" "$OUT/data" --previews "$OUT/previews"
echo "==> music (CD audio tracks)"
"$HERE/tools/stx_music.sh" "$SRC" "$OUT/music"
echo
echo "==> done:"
echo "    build/data/REPORT.TXT      the measurements"
echo "    build/data/LEVELS.TXT      the levels and their sizes"
echo "    build/previews/            every level as the 32X will draw it (ALL_LEVELS_START.png: all starts)"
echo "    build/music/Track02-13.wav the CD audio tracks"
