#!/usr/bin/env python3
"""pix2png.py FILE.pix OUTDIR [PREFIX] -- each picture in a .pix file (a u32
frame, u16 width and height, then B G R A a pixel: mp_host's and
mpdump.lua's) as OUTDIR/PREFIXnnnnn.png, turned as MAME shows Mappy
(ROT90: 224 wide, 288 high)."""
import os, struct, sys
sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', '..', 'tools'))
import png256


def main():
    d = open(sys.argv[1], 'rb').read()
    out = sys.argv[2]
    prefix = sys.argv[3] if len(sys.argv) > 3 else ''
    os.makedirs(out, exist_ok=True)
    i = 0
    while i + 8 <= len(d):
        n, w, h = struct.unpack_from('<IHH', d, i)
        px = d[i + 8:i + 8 + w * h * 4]
        i += 8 + w * h * 4
        # ROT90 (clockwise): the turned picture's (x, y) is the raw (y', x)
        # with y' = h - 1 - x
        rows = [[(px[((h - 1 - x) * w + y) * 4 + 2], px[((h - 1 - x) * w + y) * 4 + 1], px[((h - 1 - x) * w + y) * 4])
                 for x in range(h)] for y in range(w)]
        png256.write(os.path.join(out, '%s%05d.png' % (prefix, n)), rows)


if __name__ == '__main__':
    main()
