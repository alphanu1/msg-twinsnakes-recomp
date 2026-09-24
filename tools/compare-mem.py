#!/usr/bin/env python3
"""Put two MEM1 snapshots side by side, and say WHERE they differ.

WHY THIS EXISTS. Phase 3's exit criterion is a frame comparison against
Dolphin, and the design document says how: "Record input in Dolphin, replay
in the port, compare guest-memory checksums at fixed frames." Both halves
of that now exist -

    the port     MGS_MEM_DUMP=<prefix> MGS_MEM_AT=60,200,800
    Dolphin      DOLPHIN_FRAME_DUMP=<prefix> DOLPHIN_FRAME_AT=60,200,800
                 (tools/dolphin-watch.py <disc.iso>)

- and both trigger on the SAME clock: the SDK's own retrace count, which
the game keeps at 0x8027DD6C in this build. The port resolves that address
from r13 at run time and prints it; nothing here is hard-coded to a guess.

WHAT IT REPORTS, and why not a single checksum. "The snapshots differ" is
useless: 24 MB of two runs will always differ somewhere - heap addresses,
timers, uninitialised padding. What localises a divergence is WHICH pages
differ and how much of each, sorted, with the nearest preceding symbol
named. A run of identical pages either side of one bad region is a finding;
a uniform smear is a different one, and the shape says which.

    compare-mem.py <a.mem> <b.mem> [--page 4096] [--top 40]
                   [--symbols config/symbols/main.dol.symbols.txt]
                   [--region 0x81200000:0x60000]

`--region` restricts the report to one span. `--xfb <addr>:<w>x<h>` goes
further and reads that span as an EXTERNAL FRAMEBUFFER - YUV 4:2:2, two
bytes a pixel, Y0 Cb Y1 Cr - converting both sides to RGB and reporting the
picture difference the way tools/compare-frames does for two of our own
renders. That is phase 3's exit criterion stated directly: how far is our
picture from the emulator's, at the same frame of the same game.

It also writes <out>_a.ppm and <out>_b.ppm with `--ppm <out>`, because a
number says how much and a picture says what.

MEM1 is 24 MB based at 0x80000000.
"""
import sys

MEM1_BASE = 0x80000000
MEM1_SIZE = 0x1800000


def load_symbols(path):
    """(addr, name) sorted, from config/symbols/*.symbols.txt."""
    syms = []
    try:
        with open(path) as f:
            for line in f:
                parts = line.split()
                if len(parts) >= 4 and parts[1].startswith('0x'):
                    try:
                        syms.append((int(parts[1], 16), parts[3]))
                    except ValueError:
                        pass
    except OSError:
        return []
    syms.sort()
    return syms


def nearest(syms, addr):
    lo, hi = 0, len(syms)
    while lo < hi:
        mid = (lo + hi) // 2
        if syms[mid][0] <= addr:
            lo = mid + 1
        else:
            hi = mid
    if lo == 0:
        return ''
    a, n = syms[lo - 1]
    # A SYMBOL A MEGABYTE AWAY IS NOT A SYMBOL. The map covers .text and the
    # named globals; the heaps above them have no symbol at all, and
    # printing "__GXData+0x1511A00" for an address in the movie's buffers
    # is worse than printing nothing, because it reads like an answer.
    if addr - a > 0x8000:
        return ''
    return '%s+0x%X' % (n, addr - a)


def yuv_to_rgb(y, cb, cr):
    """ITU-R BT.601, the inverse of runtime/gx/efb.c's rgb_to_ycbcr.

    The same matrix, read backwards, so a difference reported here is a
    difference in the PICTURE and not in two conversions disagreeing."""
    c = y - 16
    d = cb - 128
    e = cr - 128
    r = (298 * c + 409 * e + 128) >> 8
    g = (298 * c - 100 * d - 208 * e + 128) >> 8
    b = (298 * c + 516 * d + 128) >> 8
    return (0 if r < 0 else 255 if r > 255 else r,
            0 if g < 0 else 255 if g > 255 else g,
            0 if b < 0 else 255 if b > 255 else b)


def decode_xfb(buf, off, w, h):
    """One external framebuffer as a flat RGB bytearray."""
    out = bytearray(w * h * 3)
    stride = w * 2
    for row in range(h):
        src = off + row * stride
        dst = row * w * 3
        for x in range(0, w, 2):
            i = src + x * 2
            if i + 3 >= len(buf):
                return out
            y0, cb, y1, cr = buf[i], buf[i + 1], buf[i + 2], buf[i + 3]
            r, g, b = yuv_to_rgb(y0, cb, cr)
            out[dst] = r; out[dst + 1] = g; out[dst + 2] = b
            r, g, b = yuv_to_rgb(y1, cb, cr)
            out[dst + 3] = r; out[dst + 4] = g; out[dst + 5] = b
            dst += 6
    return out


def write_ppm(path, rgb, w, h):
    with open(path, 'wb') as f:
        f.write(b'P6\n%d %d\n255\n' % (w, h))
        f.write(bytes(rgb))


def compare_xfb(a, b, spec, ppm):
    addr, _, size = spec.partition(':')
    addr = int(addr, 0)
    w, _, h = size.partition('x')
    w, h = int(w, 0), int(h, 0)
    off = addr - MEM1_BASE if addr >= MEM1_BASE else addr
    ra = decode_xfb(a, off, w, h)
    rb = decode_xfb(b, off, w, h)
    n = w * h
    diff = big = acc = 0
    for k in range(0, n * 3, 3):
        d0 = abs(ra[k] - rb[k])
        d1 = abs(ra[k + 1] - rb[k + 1])
        d2 = abs(ra[k + 2] - rb[k + 2])
        m = d0 if d0 > d1 else d1
        if d2 > m:
            m = d2
        if m:
            diff += 1
        if m > 8:
            big += 1
        acc += d0 + d1 + d2
    print('  framebuffer 0x%08X, %dx%d, YUV 4:2:2:' % (addr, w, h))
    print('    %6.2f%% of pixels differ, %6.2f%% by more than 8, '
          'mean abs error %6.2f'
          % (100.0 * diff / n, 100.0 * big / n, acc / (n * 3.0)))
    if ppm:
        write_ppm(ppm + '_a.ppm', ra, w, h)
        write_ppm(ppm + '_b.ppm', rb, w, h)
        print('    wrote %s_a.ppm and %s_b.ppm' % (ppm, ppm))


def main():
    args = [a for a in sys.argv[1:] if not a.startswith('--')]
    opts = {}
    argv = sys.argv[1:]
    for i, a in enumerate(argv):
        if a.startswith('--') and i + 1 < len(argv):
            opts[a[2:]] = argv[i + 1]
            if argv[i + 1] in args:
                args.remove(argv[i + 1])
    if len(args) < 2:
        print(__doc__)
        return 2

    page = int(opts.get('page', '4096'), 0)
    top = int(opts.get('top', '40'), 0)
    syms = load_symbols(opts.get('symbols',
                                 'config/symbols/main.dol.symbols.txt'))

    a = open(args[0], 'rb').read()
    b = open(args[1], 'rb').read()
    n = min(len(a), len(b))
    if len(a) != len(b):
        print('sizes differ: %d and %d; comparing the first %d'
              % (len(a), len(b), n))

    lo, hi = 0, n
    if 'region' in opts:
        base, _, length = opts['region'].partition(':')
        base = int(base, 0)
        lo = base - MEM1_BASE if base >= MEM1_BASE else base
        hi = lo + (int(length, 0) if length else page)
        lo = max(0, min(lo, n))
        hi = max(lo, min(hi, n))

    if 'xfb' in opts:
        print('%s vs %s' % (args[0], args[1]))
        compare_xfb(a, b, opts['xfb'], opts.get('ppm'))
        return 0

    pages = []
    total_diff = 0
    for off in range(lo, hi, page):
        end = min(off + page, hi)
        pa, pb = a[off:end], b[off:end]
        if pa == pb:
            continue
        d = sum(1 for x, y in zip(pa, pb) if x != y)
        total_diff += d
        pages.append((d, off, end - off))

    span = hi - lo
    print('%s vs %s' % (args[0], args[1]))
    print('  span 0x%08X..0x%08X (%d bytes), page %d'
          % (MEM1_BASE + lo, MEM1_BASE + hi, span, page))
    print('  %d of %d pages differ (%.2f%%), %d of %d bytes (%.4f%%)'
          % (len(pages), (span + page - 1) // page,
             100.0 * len(pages) / max(1, (span + page - 1) // page),
             total_diff, span, 100.0 * total_diff / max(1, span)))
    if not pages:
        print('  IDENTICAL over this span')
        return 0

    # The WORST pages, because a smear and a hot spot need different answers.
    pages.sort(reverse=True)
    print('  worst %d pages:' % min(top, len(pages)))
    for d, off, size in pages[:top]:
        print('    0x%08X  %5d/%d bytes (%5.1f%%)  %s'
              % (MEM1_BASE + off, d, size, 100.0 * d / size,
                 nearest(syms, MEM1_BASE + off)))

    # And the CONTIGUOUS runs, which is what says "one structure" rather
    # than "everywhere a little".
    pages.sort(key=lambda p: p[1])
    runs, cur = [], None
    for d, off, size in pages:
        if cur and off == cur[1] + cur[2]:
            cur = (cur[0] + d, cur[1], cur[2] + size)
        else:
            if cur:
                runs.append(cur)
            cur = (d, off, size)
    if cur:
        runs.append(cur)
    runs.sort(reverse=True, key=lambda r: r[2])
    print('  largest contiguous differing runs:')
    for d, off, size in runs[:min(top, len(runs))]:
        print('    0x%08X..0x%08X  %7d bytes, %5.1f%% differing  %s'
              % (MEM1_BASE + off, MEM1_BASE + off + size, size,
                 100.0 * d / size, nearest(syms, MEM1_BASE + off)))
    return 0


if __name__ == '__main__':
    sys.exit(main())
