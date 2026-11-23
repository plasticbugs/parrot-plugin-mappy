#!/bin/sh
# compare.sh [attract|game] [frames] -- the board model against MAME 0.288,
# frame by frame: video RAM, work RAM (the sprites in it), the 15XX's
# registers and shared RAM, the 58XXs, both CPUs' registers at every
# VBLANK, and every 60th frame's picture.
#
#   attract  no inputs (default 3600 frames)
#   game     a coin, one player, then mp_host's bot plays (bot.h) until
#            its lives are gone (default 7800 frames); its inputs are
#            recorded as a script and MAME plays the same script
#
# MAME runs headless. Everything goes under $TMPDIR (MP_WORK). MP_ROMS is
# the mappy.zip to use.
set -e
here=$(cd "$(dirname "$0")" && pwd)
src=$(cd "$here/../../src" && pwd)
lib=$(cd "$here/../../lib" && pwd)
name=${1:-attract}
case $name in
attract) frames=${2:-3600} ;;
game) frames=${2:-7800} ;;
*) echo "compare.sh: attract or game" >&2; exit 2 ;;
esac
work=${MP_WORK:-${TMPDIR:-/tmp}/mappy-test}
set_dir=$("$here/setup.sh")
out="$work/compare-$name"
rm -rf "${out:?}"; mkdir -p "$out"
cc -O2 -Wall -I"$src" -I"$lib" -I"$here" -o "$out/mp_host" "$here/mp_host.c" "$src/mp.c" "$src/mpsnd.c" -lm
if [ "$name" = game ]; then
    printf '300 coins 01\n306 coins 00\n360 buttons 04\n366 buttons 00\n' > "$out/start.txt"
    "$out/mp_host" "$set_dir" -frames "$frames" -script "$out/start.txt" -bot 420 -record "$out/script.txt" 2>/dev/null
else
    : > "$out/script.txt"
fi
"$out/mp_host" "$set_dir" -frames "$frames" -script "$out/script.txt" -dump "$out/model.dump" \
    -pix "$out/model.pix" -pixevery 60
"$here/mamerun.sh" "$out" "$out/script.txt" "$frames" 60
python3 -I "$here/cmpdump.py" "$out/mame.dump" "$out/model.dump" "$out/mame.pix" "$out/model.pix"
