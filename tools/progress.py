#!/usr/bin/env python3
"""Phase 0 progress, measured five ways.

Run before updating HANDOFF.md so the figures there are computed rather than
remembered. "Percent decompiled" has no single honest answer for a
recompilation - we are not producing matching source - so five measures are
reported and averaged.

The average is an unweighted mean of five dissimilar measures. It is a
headline, not a statistic: read the rows.
"""
import re, collections, sys, os

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
def P(*p): return os.path.join(ROOT, *p)

def funcs(path):
    d = {}
    for l in open(path):
        m = re.match(r'^\S+ = (\.\w+):0x([0-9A-Fa-f]+); // type:function size:0x([0-9A-Fa-f]+)', l)
        if m: d[int(m.group(2), 16)] = int(m.group(3), 16)
    return d

def main():
    dol = funcs(P('build/phase0/main.symbols.txt'))
    rel = funcs(P('build/phase0/rel.symbols.txt'))
    named = {}
    for l in open(P('config/symbols/main.dol.symbols.txt')):
        m = re.match(r'^\S+ 0x([0-9A-Fa-f]+) 0x\S+ (\S+)', l)
        if m: named[int(m.group(1), 16)] = m.group(2)
    sites = collections.Counter()
    for l in open(P('build/phase0/out/mgso_pal/asm/auto_00_00000000_text.s')):
        for t in re.findall(r'\bbl fn_(80[0-9A-Fa-f]{6})', l):
            sites[int(t, 16)] += 1
    ref_gx = {m.group(1) for m in
              (re.match(r'(\S+) = \.text:.*type:function', l)
               for l in open(P('extern/mkdd/config/MarioClub_us/symbols.txt')))
              if m and re.match(r'_{0,2}GX', m.group(1))}
    gx = [n for n in named.values() if re.match(r'_{0,2}GX', n)]

    total = len(dol) + len(rel)
    rows = [
        ("Functions named",                          len(named.keys() & dol.keys()), total),
        ("Function boundaries recovered",            total, total),
        ("SDK entry points the engine calls, named", len([a for a in sites if a in named]), len(sites)),
        ("SDK call sites covered",                   sum(sites[a] for a in sites if a in named), sum(sites.values())),
        ("GX surface named",                         len(gx), len(ref_gx)),
    ]
    print(f"| {'Measure':<42} | {'':>15} | {'':>6} |")
    print(f"|{'-'*44}|{'-'*17}|{'-'*8}|")
    acc = 0
    for name, a, b in rows:
        pct = 100 * a / b; acc += pct
        print(f"| {name:<42} | {a:>6} / {b:<6} | {pct:>5.1f}% |")
    print(f"| **{'Average of the five':<40}** | {'':>15} | **{acc/len(rows):.1f}%** |")

if __name__ == '__main__':
    main()
