#!/usr/bin/env python3
"""Attribute unnamed SDK functions to a library by address locality.

Stage 5c of docs/decompilation-process.md.

SDK libraries link contiguously: every GX object file lands beside the other
GX object files. So an unnamed function bracketed by two NAMED functions that
both belong to the same library is almost certainly in that library too.

This assigns no names. It answers a different and still useful question: of
the SDK entry points the engine calls but we cannot name, which library is
each one in - and therefore which part of the runtime has to implement it.

A function is attributed only when BOTH neighbours agree. Where they disagree
it sits on a library boundary and is left unattributed rather than guessed.
"""
import re, bisect, collections, argparse

# Longest prefix wins. DBG and EXI2_ are the debugger mailbox library and live
# in a different part of the binary from DB and EXI - lumping them together
# stretched both ranges across 190 KB and made every range overlap.
LIBS = ['DBG', 'EXI2_', 'GX', 'OS', 'DVD', 'AX', 'AI', 'DSP', 'AR', 'ARQ',
        'CARD', 'PAD', 'VI', 'SI', 'EXI', 'PS', 'MTX', 'DB', 'TRK', 'GD', 'THP']

def lib_of(name):
    n = name.lstrip('_')
    for p in sorted(LIBS, key=len, reverse=True):
        if n.startswith(p):
            return p.rstrip('_')
    return None

def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--symbols', required=True)
    ap.add_argument('--asm', required=True, help='REL disassembly, for call sites')
    a = ap.parse_args()

    named = {}
    for l in open(a.symbols):
        m = re.match(r'^\S+ 0x([0-9A-Fa-f]+) 0x\S+ (\S+)', l)
        if m: named[int(m.group(1), 16)] = m.group(2)
    addrs = sorted(named)

    sites = collections.Counter()
    for l in open(a.asm):
        for t in re.findall(r'\bbl fn_(80[0-9A-Fa-f]{6})', l):
            sites[int(t, 16)] += 1

    # Library ranges, and which of them are unambiguous. Some libraries genuinely
    # interleave - SI sits inside OS's span - so a range is only usable for
    # attribution if no other library's range overlaps it.
    spans = collections.defaultdict(list)
    for ad, nm in named.items():
        L = lib_of(nm)
        if L: spans[L].append(ad)
    # Trimmed range, not min/max. A single symbol filed under the wrong library
    # stretches a range across the whole binary and makes every other range
    # look like it overlaps - __DBVECTOR alone did exactly that, swallowing
    # DVD, VI and GX. Dropping the outermost 10% each side is enough.
    def trimmed(v):
        v = sorted(v)
        k = max(1, len(v) // 10)
        return (v[k], v[-1 - k]) if len(v) > 2 * k + 1 else (v[0], v[-1])
    rng = {L: trimmed(v) for L, v in spans.items() if len(v) >= 5}
    clean = {}
    for L, (lo, hi) in rng.items():
        if not any(o != L and lo2 < hi and lo < hi2 for o, (lo2, hi2) in rng.items()):
            clean[L] = (lo, hi)
    print("unambiguous library ranges (no overlap with any other):")
    for L, (lo, hi) in sorted(clean.items(), key=lambda x: x[1][0]):
        print(f"  {L:<6} 0x{lo:08X} .. 0x{hi:08X}")
    print("interleaved, not usable for attribution:",
          ", ".join(sorted(set(rng) - set(clean))) or "none")
    print()

    attributed, boundary, out_of_range = collections.Counter(), 0, 0
    weighted = collections.Counter()
    for addr, n in sites.items():
        if addr in named:
            continue
        hit = [L for L, (lo, hi) in clean.items() if lo <= addr <= hi]
        if len(hit) == 1:
            attributed[hit[0]] += 1
            weighted[hit[0]] += n
            continue
        i = bisect.bisect_left(addrs, addr)
        if i == 0 or i >= len(addrs):
            out_of_range += 1; continue
        before, after = lib_of(named[addrs[i-1]]), lib_of(named[addrs[i]])
        if before and before == after:
            attributed[before] += 1
            weighted[before] += n
        else:
            boundary += 1

    unnamed = len([a_ for a_ in sites if a_ not in named])
    print(f"SDK entry points the engine calls, unnamed   {unnamed:>5}")
    print(f"  attributed to a library                    {sum(attributed.values()):>5}"
          f"  ({100*sum(attributed.values())/unnamed:.0f}%)")
    print(f"  on a library boundary (left alone)         {boundary:>5}")
    print(f"  outside the named range                    {out_of_range:>5}")
    print()
    print(f"{'library':<10}{'functions':>10}{'call sites':>12}")
    for lib, c in attributed.most_common():
        print(f"{lib:<10}{c:>10}{weighted[lib]:>12}")

if __name__ == '__main__':
    main()
