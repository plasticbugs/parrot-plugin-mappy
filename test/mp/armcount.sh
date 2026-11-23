#!/bin/sh
# armcount.sh [armcount.py's options] -- what a list costs the cart: the
# board, the 15XX, the picture and the list (test/mp/bench.c, the same
# mprun.c the plugin runs) built for the Cortex-M33 with the plugin's
# flags (CMakeLists.txt: the board -O3 -fno-data-sections, the picture
# -O2), counted by the kit's tools/armcount on Unicorn. The game plays
# itself: a coin, a start, then the tests' bot. MP_ROMS is the mappy.zip;
# ARM_PY a Python with tools/armcount/requirements.txt (docs/testing.md).
#
#   test/mp/armcount.sh --lists 2400 --from 300 --profile
set -e
here=$(cd "$(dirname "$0")" && pwd)
root=$(cd "$here/../.." && pwd)
work=${MP_WORK:-${TMPDIR:-/tmp}/mappy-test}
set_dir=$("$here/setup.sh")
out="$work/arm"
mkdir -p "$out"
# the set as bench.c lays it out in bench_data
for f in mpx_3.1d mp1_2.1c mpx_1.1b mp1_4.1k mp1_5.3b mp1_6.3m mp1_7.3n mp1-5.5b mp1-6.4c mp1-7.5k mp1-3.3m; do
    cat "$set_dir/$f"
done > "$out/roms.bin"
cflags="-mcpu=cortex-m33 -mthumb -march=armv8-m.main+fp+dsp -mfloat-abi=softfp -std=c11 -ffunction-sections \
    -DPICO_ON_DEVICE=1 -I$root/src -I$root/lib -I$here -I$root/sdk/firmware/revc/sdk"
objs=
for f in "$root/src/mp.c" "$root/src/mpsnd.c" "$root/src/mprun.c" "$root/src/mpcv.c" "$root/sdk/firmware/revc/sdk/cvscale.c"; do
    case $(basename "$f") in
        mpcv.c|cvscale.c) opt="-O2" ;;
        *) opt="-O3 -fno-data-sections" ;;
    esac
    o="$out/$(basename "$f" .c).o"
    # shellcheck disable=SC2086
    arm-none-eabi-gcc $cflags -fdata-sections $opt -c -o "$o" "$f"
    objs="$objs $o"
done
# shellcheck disable=SC2086
"$root/tools/armcount/build.sh" "$out/bench.elf" "$here/bench.c" $objs -- -O2 -DPICO_ON_DEVICE=1 -I"$root/src" -I"$here"
arm-none-eabi-size "$out/bench.elf"
exec "${ARM_PY:-python3}" "$root/tools/armcount/armcount.py" "$out/bench.elf" --load bench_data="$out/roms.bin" "$@"
