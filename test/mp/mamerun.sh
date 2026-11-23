#!/bin/sh
# mamerun.sh OUTDIR SCRIPT FRAMES [PIXEVERY] -- MAME's mappy, headless,
# with mpdump.lua: OUTDIR/mame.dump (and mame.pix). MP_WAV set: MAME's
# sound to that file too (-wavwrite, 48 kHz). MP_TRACE set: a CPU's trace
# too (mpdump.lua; MAME's debugger on, headless). MP_ROMS is the set's zip.
#
# MAME runs headless, always: no window, no sound, as fast as it goes, and
# a time limit so it can't hang.
set -e
here=$(cd "$(dirname "$0")" && pwd)
out=$1; script=$2; frames=$3; pixevery=${4:-0}
roms=${MP_ROMS:?MP_ROMS is the mappy.zip to use}
mkdir -p "$out"
rp=$(mktemp -d "${TMPDIR:-/tmp}/mprp.XXXXXX")
cp "$roms" "$rp/mappy.zip"
mkdir "$rp/cfg" "$rp/nvram"
pix=
[ "$pixevery" -gt 0 ] && pix="$out/mame.pix"
secs=$(( frames / 60 + 30 ))
MP_SCRIPT="$script" MP_DUMP="$out/mame.dump" MP_FRAMES="$frames" MP_PIX="$pix" MP_PIXEVERY="$pixevery" \
SDL_VIDEODRIVER=dummy mame mappy -rompath "$rp" ${MP_TRACE:+-debug -debugger none} \
    -video none -videodriver dummy -window -nomaximize -sound none \
    -nothrottle -frameskip 0 -skip_gameinfo -seconds_to_run "$secs" \
    -cfg_directory "$rp/cfg" -nvram_directory "$rp/nvram" -snapshot_directory "$rp/snap" \
    -autoboot_delay 0 -autoboot_script "$here/mpdump.lua" ${MP_WAV:+-wavwrite "$MP_WAV"} >"$out/mame.log" 2>&1 || true
rm -rf "${rp:?}"
