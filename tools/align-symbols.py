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
import re, sys, argparse, collections
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

    # Pass 2: re-sync across gaps. A run ends at a size mismatch because one
    # binary links a function the other does not. Rather than stopping there,
    # look ahead a bounded distance in each list for a point where the next
    # CONFIRM sizes all agree, and resume from it.
    #
    # The guard is what makes this safe: a lone size coincidence is common
    # (many functions are 0x20 bytes), but CONFIRM consecutive sizes agreeing
    # by chance is not. Resync is refused unless the whole window matches.
    LOOKAHEAD, CONFIRM = 24, 3

    def window_matches(i, j):
        if i + CONFIRM > len(ours) or j + CONFIRM > len(ref):
            return False
        return all(ours[i + k][1] == ref[j + k][1] for k in range(CONFIRM))

    resyncs = 0
    for i0, j0 in anchors:
        i, j = i0 + 1, j0 + 1
        while i < len(ours) and j < len(ref):
            if ours[i][1] == ref[j][1]:
                name = ref[j][2]
                if unnamed(ours[i][2]) and not unnamed(name):
                    prev = assigned.get(ours[i][0])
                    if prev and prev != name:
                        del assigned[ours[i][0]]
                        break
                    assigned[ours[i][0]] = name
                i += 1; j += 1
                continue
            # mismatch: try to resync, preferring the smallest skip
            best = None
            for di in range(0, LOOKAHEAD):
                for dj in range(0, LOOKAHEAD):
                    if di == 0 and dj == 0:
                        continue
                    if window_matches(i + di, j + dj):
                        best = (di + dj, i + di, j + dj)
                        break
                if best:
                    break
            if not best:
                break
            _, i, j = best
            resyncs += 1

    # Pass 3: walk outward from each anchor while sizes agree. Catches the
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
    return anchors, assigned, runs, segments, resyncs

def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--ours', required=True)
    ap.add_argument('--ref', required=True, nargs='+',
                    help='one or more reference symbol maps; with several, a name is accepted only by consensus')
    ap.add_argument('--section', default='.text')
    ap.add_argument('--prefix', help='only report/assign names with this prefix')
    ap.add_argument('--out')
    a = ap.parse_args()

    ours = load(a.ours, a.section)
    votes = collections.defaultdict(dict)      # addr -> {reference: name}
    for rp in a.ref:
        ref = load(rp, a.section)
        anchors, assigned, runs, segments, resyncs = align(ours, ref)
        label = rp.split('/')[1] if '/' in rp else rp
        print(f"{label:<18} {len(ref):>6} funcs {len(anchors):>4} anchors "
              f"{segments:>3} seg {resyncs:>5} resync {len(assigned):>4} names")
        for addr, name in assigned.items():
            votes[addr][label] = name

    # Consensus. A name backed by two or more independent references is strong
    # evidence: different games, linked differently, agreeing only because the
    # function really is that function. Where references DISAGREE about an
    # address every claim is discarded - a contradiction means at least one is
    # wrong and nothing here can say which.
    assigned, conflicts, single = {}, 0, 0
    for addr, byref in votes.items():
        names = set(byref.values())
        if len(names) > 1:
            conflicts += 1
            continue
        name = names.pop()
        if len(byref) == 1:
            single += 1
        assigned[addr] = name

    if a.prefix:
        assigned = {k: v for k, v in assigned.items() if v.startswith(a.prefix)}

    print()
    print(f"ours            {len(ours):>6} functions in {a.section}")
    print(f"references      {len(a.ref):>6}")
    print(f"names agreed    {len(assigned) - single:>6} (2+ references concur)")
    print(f"names single    {single:>6} (only one reference names it)")
    print(f"CONFLICTS       {conflicts:>6} (references disagree - all discarded)")
    print(f"names assigned  {len(assigned):>6}" + (f" (prefix {a.prefix})" if a.prefix else ""))

    if a.out:
        size_of = {r[0]: r[1] for r in ours}
        with open(a.out, 'w') as f:
            for addr in sorted(assigned):
                f.write(f".text 0x{addr:08X} 0x{size_of[addr]:X} {assigned[addr]} mkdd-align\n")
        print(f"wrote {a.out}")

if __name__ == '__main__':
    main()
