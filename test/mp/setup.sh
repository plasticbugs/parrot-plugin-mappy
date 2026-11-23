#!/bin/sh
# setup.sh -- the set unzipped under $TMPDIR for mp_host (prints the folder).
# MP_ROMS is the mappy.zip to use.
set -e
roms=${MP_ROMS:?MP_ROMS is the mappy.zip to use}
work=${MP_WORK:-${TMPDIR:-/tmp}/mappy-test}
dir="$work/set"
mkdir -p "$dir"
unzip -o -q "$roms" -d "$dir"
echo "$dir"
