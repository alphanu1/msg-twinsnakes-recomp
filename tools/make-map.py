#!/usr/bin/env python3
"""Emit a linker MAP from config/symbols/ for DolRecomp's --map option.

Stage 7 support, docs/decompilation-process.md.

DolRecomp names generated functions from a MAP, so the 295 MB of generated C
reads as GXInit rather than fn_8003F070. That matters for phase 1: when the
boot fails, the backtrace should name the SDK function it failed in.

Accepts DolRecomp's simplest MAP form - address, size, name, all hex - and
skips zero-size entries, which carry no extent for it to attach a name to.

--map is DOL input only, which suits us: the REL has no names to give it.
"""
import re, sys, argparse

def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--symbols', required=True)
    ap.add_argument('--out', required=True)
    ap.add_argument('--section', default='.text')
    a = ap.parse_args()

    rows, skipped = [], 0
    for line in open(a.symbols):
        m = re.match(r'^(\.\w+) 0x([0-9A-Fa-f]+) (0x[0-9A-Fa-f]+|\?) (\S+)', line)
        if not m:
            continue
        sec, addr, size, name = m.group(1), int(m.group(2), 16), m.group(3), m.group(4)
        if a.section and sec != a.section:
            continue
        if size == '?' or int(size, 16) == 0:
            skipped += 1
            continue
        rows.append((addr, int(size, 16), name))

    rows.sort()
    with open(a.out, 'w') as f:
        for addr, size, name in rows:
            f.write(f"{addr:08x} {size:08x} {name}\n")
    print(f"wrote {len(rows)} symbols to {a.out} ({skipped} skipped: no size)")

if __name__ == '__main__':
    main()
