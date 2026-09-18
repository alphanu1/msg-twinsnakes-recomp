#!/usr/bin/env python3
"""Classify REL engine functions by the SDK they call.

Stage 6 support, docs/decompilation-process.md.

The REL is Konami's engine: it appears in no other binary, so no signature
database can name it. What it does do is call into the DOL's SDK through
cross-module relocations, and the DOL's SDK is now 851 named symbols.

So a REL function that calls GXSetTevOp is renderer code, one that calls
DVDReadAsync is file loading, one that calls AXSetVoiceSrc is audio. That
does not give a function its name, but it tells a human where to start
reading - which is the expensive part of stage 6.

This assigns no names. It produces a map of where to look.
"""
import re, sys, argparse, collections

FN   = re.compile(r'^\.fn (\S+?), ')
CALL = re.compile(r'\bbl (fn_80[0-9A-Fa-f]{6}|[A-Za-z_]\w*)')
SYM  = re.compile(r'^\S+ 0x([0-9A-Fa-f]+) 0x[0-9A-Fa-f]+ (\S+)')

LIBS = [('GX','renderer'), ('OS','os/threads'), ('DVD','file i/o'),
        ('AX','audio'), ('AI','audio'), ('DSP','audio'), ('AR','aram'),
        ('ARQ','aram'), ('CARD','saves'), ('PAD','input'), ('VI','video'),
        ('SI','input'), ('EXI','bus'), ('MTX','matrix'), ('PS','matrix')]

def lib_of(name):
    n = name.lstrip('_')
    for pre, area in LIBS:
        if n.startswith(pre) and (len(n) == len(pre) or not n[len(pre)].islower()):
            return pre, area
    return None, None

def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--asm', required=True, help='REL disassembly (.s)')
    ap.add_argument('--symbols', required=True, help='DOL symbol map')
    ap.add_argument('--out')
    ap.add_argument('--top', type=int, default=12)
    a = ap.parse_args()

    dol = {}
    for line in open(a.symbols):
        m = SYM.match(line)
        if m: dol['fn_' + m.group(1).upper()] = m.group(2)

    cur, calls = None, collections.defaultdict(set)
    for line in open(a.asm):
        m = FN.match(line)
        if m: cur = m.group(1); continue
        if cur is None: continue
        for t in CALL.findall(line):
            key = ('fn_' + t[3:].upper()) if t.startswith('fn_') else t
            name = dol.get(key, t)
            if not name.startswith('fn_'):
                calls[cur].add(name)

    areas = collections.Counter()
    libs  = collections.Counter()
    rows  = []
    for fn, names in calls.items():
        seen = collections.Counter()
        for n in names:
            pre, area = lib_of(n)
            if pre: seen[area] += 1; libs[pre] += 1
        if not seen: continue
        area = seen.most_common(1)[0][0]
        areas[area] += 1
        rows.append((fn, area, sorted(names)))

    print(f"REL functions calling named SDK   {len(rows):>6}")
    print(f"distinct SDK functions called     {len({n for _,_,ns in rows for n in ns}):>6}")
    print()
    print("engine functions by area:")
    for area, n in areas.most_common():
        print(f"  {area:<14}{n:>6}")
    print()
    print(f"most-called SDK libraries: " + ", ".join(f"{k}({v})" for k, v in libs.most_common(a.top)))

    if a.out:
        with open(a.out, 'w') as f:
            f.write("# REL engine functions classified by the SDK they call.\n")
            f.write("# Produced by tools/classify-rel.py. Assigns no names - this is\n")
            f.write("# a map of where to look, for stage 6 analysis in Ghidra.\n")
            f.write("# Format: <rel-function> <area> <sdk functions called>\n\n")
            for fn, area, names in sorted(rows):
                f.write(f"{fn} {area} {','.join(names)}\n")
        print(f"\nwrote {a.out}")

if __name__ == '__main__':
    main()
