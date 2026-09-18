#!/usr/bin/env python3
"""Name SDK functions by ordered run alignment against a reference decomp.

Stage 5 of docs/decompilation-process.md.

Two GameCube games built against the same Dolphin SDK build link identical
machine code for the SDK functions they share. They do NOT share a single
address offset: each game links only the functions it references, so the
delta between the binaries is piecewise constant, shifting wherever one
binary omits a function the other keeps.

So we anchor on functions already named in both, then walk outward in
lockstep while function sizes agree. A size disagreement ends the run.
Names are only ever transferred onto unnamed (fn_*) functions, and only
when the size matches exactly.

No code is copied - only names, and only where our own binary's function
sizes independently confirm the match.
"""
import re, sys, argparse
from collections import defaultdict

SYM = re.compile(r'^(\S+) = (\.\w+):0x([0-9A-Fa-f]+); // type:function size:0x([0-9A-Fa-f]+)')

def load(path, section=None):
    out = []
    for line in open(path):
        m = SYM.match(line.strip())
        if not m: continue
        name, sec, addr, size = m.group(1), m.group(2), int(m.group(3), 16), int(m.group(4), 16)
        if section and sec != section: continue
        out.append([addr, size, name])
    out.sort(key=lambda r: r[0])
    return out

def unnamed(n): return n.startswith('fn_') or n.startswith('lbl_')

def align(ours, ref, verbose=False):
    ref_by_name = {r[2]: i for i, r in enumerate(ref)}
    # anchors: same name in both, and the same size (size proves same codegen)
    anchors = []
    for i, (a, s, n) in enumerate(ours):
        if unnamed(n): continue
        j = ref_by_name.get(n)
        if j is not None and ref[j][1] == s:
            anchors.append((i, j))
    assigned = {}
    runs = []

    # Pass 1: between-anchor segments. Two consecutive anchors bracket a
    # segment in each binary. If the segments have the same length AND every
    # size agrees pairwise, the linker kept the same functions in the same
    # order between those two points, and the mapping is 1:1. This is the
    # strongest evidence available without the reference binary itself.
    segments = 0
    for (ia, ja), (ib, jb) in zip(anchors, anchors[1:]):
        na, nb = ib - ia, jb - ja
        if na != nb or na <= 1:
            continue
        pairs = [(ours[ia + k], ref[ja + k]) for k in range(1, na)]
        if any(o[1] != r[1] for o, r in pairs):
            continue
        segments += 1
        for o, r in pairs:
            if unnamed(o[2]) and not unnamed(r[2]):
                assigned[o[0]] = r[2]

    # Pass 2: walk outward from each anchor while sizes agree. Catches the
    # regions beyond the first and last anchor, and segments that pass 1
    # rejected for length.
    for i0, j0 in anchors:
        for step in (1, -1):
            i, j, length = i0 + step, j0 + step, 0
            while 0 <= i < len(ours) and 0 <= j < len(ref):
                if ours[i][1] != ref[j][1]:
                    break                      # size disagreement ends the run
                name = ref[j][2]
                if unnamed(ours[i][2]) and not unnamed(name):
                    prev = assigned.get(ours[i][0])
                    if prev and prev != name:
                        del assigned[ours[i][0]]
                        break                  # conflicting claim: refuse both
                    assigned[ours[i][0]] = name
                length += 1
                i += step; j += step
            if length: runs.append((ours[i0][2], step, length))
    return anchors, assigned, runs, segments

def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--ours', required=True)
    ap.add_argument('--ref', required=True)
    ap.add_argument('--section', default='.text')
    ap.add_argument('--prefix', help='only report/assign names with this prefix')
    ap.add_argument('--out')
    a = ap.parse_args()

    ours = load(a.ours, a.section)
    ref = load(a.ref, a.section)
    anchors, assigned, runs, segments = align(ours, ref)

    if a.prefix:
        assigned = {k: v for k, v in assigned.items() if v.startswith(a.prefix)}

    print(f"ours            {len(ours):>6} functions in {a.section}")
    print(f"reference       {len(ref):>6} functions in {a.section}")
    print(f"anchors         {len(anchors):>6} (same name AND same size in both)")
    print(f"segments mapped {segments:>6} (consecutive anchors, exact size agreement)")
    print(f"runs extended   {len(runs):>6}")
    print(f"names assigned  {len(assigned):>6}" + (f" (prefix {a.prefix})" if a.prefix else ""))

    if a.out:
        size_of = {r[0]: r[1] for r in ours}
        with open(a.out, 'w') as f:
            for addr in sorted(assigned):
                f.write(f".text 0x{addr:08X} 0x{size_of[addr]:X} {assigned[addr]} mkdd-align\n")
        print(f"wrote {a.out}")

if __name__ == '__main__':
    main()
