#!/usr/bin/env python3
"""cmpdump.py A.dump B.dump [A.pix B.pix] -- compare mp_host's and MAME's frames.

Prints how many frames matched in each part (video RAM, work RAM, the
15XX's registers and RAM, the 58XXs, the CPUs' registers, the picture) and
the first frame and offset where each part differs. MAME can stop a 6809
in the middle of an instruction at a frame's end, so one or two bytes
that differ for one frame are counted but don't fail; the registers are
shown but never fail (MAME's PC can be inside an instruction, and its sub
CPU's reads 0000 until it leaves reset). Exit status 1 if a RAM part has
more than two bytes different in a frame, or a byte different two frames
running, or a picture differs.
"""
import struct, sys

PARTS = [('video RAM', 0x1000, 0), ('work RAM', 0x1800, 0x1000), ('15XX', 0x400, 0x4000), ('58XXs', 0x20, 0x4800),
         ('main CPU registers', 14, None), ('sub CPU registers', 14, None)]
REC = 8 + sum(n for _, n, _ in PARTS)


def frames(name):
    d = open(name, 'rb').read()
    for i in range(0, len(d) - REC + 1, REC):
        assert d[i:i + 4] == b'MPFR', 'bad record at %d' % i
        yield int.from_bytes(d[i + 4:i + 8], 'little'), d[i + 8:i + REC]


def pix(name):
    d = open(name, 'rb').read()
    i, out = 0, {}
    while i + 8 <= len(d):
        n, w, h = struct.unpack_from('<IHH', d, i)
        out[n] = (w, h, d[i + 8:i + 8 + w * h * 4])
        i += 8 + w * h * 4
    return out


def main():
    a, b = dict(frames(sys.argv[1])), dict(frames(sys.argv[2]))
    common = sorted(set(a) & set(b))
    if not common:
        print('no frames in common')
        sys.exit(1)
    print('frames compared: %d (%d..%d)' % (len(common), common[0], common[-1]))
    bad = 0
    off = 0
    for name, n, base in PARTS:
        diff = [f for f in common if a[f][off:off + n] != b[f][off:off + n]]

        def bytes_of(f):
            x, y = a[f][off:off + n], b[f][off:off + n]
            return {k for k in range(n) if x[k] != y[k]}

        # a real difference: more than two bytes in a frame, or a byte that
        # still differs a frame later (an instruction MAME stopped in the
        # middle of leaves one or two bytes of one frame)
        lasting = [f for f in diff if len(bytes_of(f)) > 2 or
                   (f + 1 in a and f + 1 in b and bytes_of(f) & bytes_of(f + 1))]
        line = '  %-20s %d/%d frames identical' % (name, len(common) - len(diff), len(common))
        if diff:
            f = (lasting or diff)[0]
            x, y = a[f][off:off + n], b[f][off:off + n]
            j = next(k for k in range(n) if x[k] != y[k])
            where = '%04X' % (base + j) if base is not None else 'byte %d' % j
            line += ', first%s difference frame %d at %s (%02X vs %02X)' % (' lasting' if lasting else '', f, where, x[j], y[j])
            if base is not None and lasting:
                bad = 1
        print(line)
        off += n
    if len(sys.argv) > 4:
        pa, pb = pix(sys.argv[3]), pix(sys.argv[4])
        both = sorted(set(pa) & set(pb))
        same, first = 0, None
        for f in both:
            if pa[f] == pb[f]:
                same += 1
            elif first is None:
                w, h, x = pa[f]
                y = pb[f][2]
                if (w, h) != pb[f][:2]:
                    first = '%d: sizes %dx%d vs %dx%d' % (f, w, h, pb[f][0], pb[f][1])
                else:
                    k = next(i for i in range(0, len(x), 4) if x[i:i + 3] != y[i:i + 3])
                    first = '%d at %d,%d (%s vs %s)' % (f, (k // 4) % w, (k // 4) // w, x[k:k + 3].hex(), y[k:k + 3].hex())
        print('  %-20s %d/%d identical%s' % ('picture', same, len(both), ', first difference ' + first if first else ''))
        if first:
            bad = 1
    sys.exit(bad)


if __name__ == '__main__':
    main()
