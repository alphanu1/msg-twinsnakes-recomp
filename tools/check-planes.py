#!/usr/bin/env python3
"""Judge a dumped GX I8 plane: is it a picture, or is it noise?

Written because "the video is garbage" and "the planes decode correctly"
(F270) cannot both be true, and neither can be settled by looking at a
screenshot - a screenshot shows the END of the chain. This looks at the
SOURCE, and answers one question: do the movie's luma and chroma planes hold
a smooth image, or do they hold noise?

The measure is the mean absolute difference between neighbouring pixels.
It separates the cases without any judgement call:

    uniform random bytes    ~85    (the theoretical mean for two uniform
                                    bytes is 255/3 = 85)
    real video luma        3-20    neighbouring pixels are nearly equal

It also decides the TILING by measurement rather than by assumption. GX
stores 8bpp textures in 8x4 tiles; if that is right, de-tiling makes the
image smoother, and if it is wrong, de-tiling makes it worse. Reporting both
means the answer does not rest on my believing the geometry is 8x4.

Usage:  check-planes.py <file> <width> <height> [--ppm <out>]
        check-planes.py --yuv <y> <u> <v> <w> <h> [--ppm <out>]

Rule 8: the dumps are game assets. They live in the scratchpad and are never
committed; this tool reads them, it does not carry them.
"""
import sys


def detile_i8(data, w, h):
    """GX 8bpp: 8x4 tiles, tiles row-major, pixels row-major within a tile."""
    out = bytearray(w * h)
    i = 0
    for ty in range(0, h, 4):
        for tx in range(0, w, 8):
            for row in range(4):
                y = ty + row
                if y >= h:
                    i += 8
                    continue
                base = y * w + tx
                n = min(8, w - tx)
                out[base:base + n] = data[i:i + n]
                i += 8
    return bytes(out)


def roughness(px, w, h):
    """Mean |difference| between horizontal and vertical neighbours."""
    ht = hn = vt = vn = 0
    for y in range(h):
        r = y * w
        for x in range(w - 1):
            ht += abs(px[r + x + 1] - px[r + x])
            hn += 1
    for y in range(h - 1):
        r = y * w
        for x in range(w):
            vt += abs(px[r + w + x] - px[r + x])
            vn += 1
    return (ht / hn if hn else 0.0), (vt / vn if vn else 0.0)


def column_profile(px, w, h):
    """Mean horizontal step at each column modulo 8.

    Vertical striping at the tile pitch shows up here and nowhere else: if
    de-tiling is wrong, the step at column 7->8 is far larger than the rest,
    because that is where the stored order jumps four rows.
    """
    tot = [0] * 8
    cnt = [0] * 8
    for y in range(h):
        r = y * w
        for x in range(w - 1):
            tot[x & 7] += abs(px[r + x + 1] - px[r + x])
            cnt[x & 7] += 1
    return [t / c if c else 0.0 for t, c in zip(tot, cnt)]


def describe(name, raw, w, h):
    lin = raw[:w * h]
    til = detile_i8(raw, w, h)
    lh, lv = roughness(lin, w, h)
    th, tv = roughness(til, w, h)
    lo, hi = min(lin), max(lin)
    print(f"{name}: {w}x{h}  range {lo}-{hi}  mean {sum(lin)/len(lin):.1f}")
    print(f"    as stored   horiz {lh:6.2f}  vert {lv:6.2f}")
    print(f"    de-tiled    horiz {th:6.2f}  vert {tv:6.2f}")
    better, px = ("de-tiled", til) if th + tv < lh + lv else ("as stored", lin)
    print(f"    smoother: {better}")
    r = (th + tv) / 2 if better == "de-tiled" else (lh + lv) / 2
    if lo == hi:
        print("    VERDICT: flat - one constant value, no picture at all")
    elif r > 60:
        print(f"    VERDICT: NOISE ({r:.1f}, random bytes measure ~85)")
    elif r > 25:
        print(f"    VERDICT: rough ({r:.1f}) - structured, but not a "
              f"natural image")
    else:
        print(f"    VERDICT: picture ({r:.1f})")
    prof = column_profile(px, w, h)
    print("    step by column mod 8: " +
          " ".join(f"{v:.0f}" for v in prof))
    # The floor matters: on a plane this smooth the steps are 0 and 1, and a
    # ratio test on numbers that small calls rounding a tiling fault. Only
    # claim the tiling is wrong when the boundary step is big ENOUGH to see.
    if prof[7] > 8 and prof[7] > 3 * (sum(prof[:7]) / 7 + 0.001):
        print("    ^ the step at the tile boundary is far larger than the "
              "rest: the tiling is WRONG")
    return px


def write_ppm(path, rgb, w, h):
    with open(path, "wb") as f:
        f.write(b"P6\n%d %d\n255\n" % (w, h))
        f.write(bytes(rgb))


def yuv_to_rgb(y, u, v, w, h):
    """BT.601 4:2:0, chroma at half resolution in both axes."""
    cw = w // 2
    out = bytearray(w * h * 3)
    for j in range(h):
        cr = (j // 2) * cw
        for i in range(w):
            yy = y[j * w + i] - 16
            uu = u[cr + i // 2] - 128
            vv = v[cr + i // 2] - 128
            r = (298 * yy + 409 * vv + 128) >> 8
            g = (298 * yy - 100 * uu - 208 * vv + 128) >> 8
            b = (298 * yy + 516 * uu + 128) >> 8
            o = (j * w + i) * 3
            out[o] = 0 if r < 0 else (255 if r > 255 else r)
            out[o + 1] = 0 if g < 0 else (255 if g > 255 else g)
            out[o + 2] = 0 if b < 0 else (255 if b > 255 else b)
    return out


def main():
    a = sys.argv[1:]
    ppm = None
    if "--ppm" in a:
        i = a.index("--ppm")
        ppm = a[i + 1]
        del a[i:i + 2]
    if a and a[0] == "--yuv":
        yf, uf, vf, w, h = a[1], a[2], a[3], int(a[4]), int(a[5])
        y = describe("luma", open(yf, "rb").read(), w, h)
        u = describe("chroma U", open(uf, "rb").read(), w // 2, h // 2)
        v = describe("chroma V", open(vf, "rb").read(), w // 2, h // 2)
        if ppm:
            write_ppm(ppm, yuv_to_rgb(y, u, v, w, h), w, h)
            print(f"wrote {ppm}")
        return 0
    f, w, h = a[0], int(a[1]), int(a[2])
    px = describe(f, open(f, "rb").read(), w, h)
    if ppm:
        write_ppm(ppm, bytes(b for p in px for b in (p, p, p)), w, h)
        print(f"wrote {ppm}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
