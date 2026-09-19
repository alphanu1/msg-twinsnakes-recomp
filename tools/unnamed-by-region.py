#!/usr/bin/env python3
"""Where are the unnamed SDK call sites, and is there anything to match them against?

"1,161 call sites unnamed" is a number that invites the wrong work. Most of
them land in Konami's own sound layer, Tremor and CR_System, where no
reference binary exists to align against and no upstream source exists to
match - every name there has to be earned one function at a time by reading
the instruction stream, and can only ever be a description of behaviour.

The call sites that land in the SDK are a different proposition entirely:
doldecomp/dolsdk2004 is a public clean-room source, so a match there is
checkable against a declaration and a body.

So this sorts the remaining work by whether a reference exists for it, and
prints the ones that can actually be identified. Aiming at the raw count
sends the effort at the three quarters that cannot move.

Regions come from config/symbols/main.dol.files.txt, which was established by
stage 5e's __FILE__ evidence - they are not guessed from address ranges.
"""
import argparse
import collections
import re

# Address ranges. `ref` is whether a public reference exists for that code -
# it is the only column that changes what work is possible.
#
# THE BOUNDARIES ARE COARSE AND TWO OF THEM ARE EVIDENCED RATHER THAN GUESSED.
# 0x80046EF0 is where config/symbols/main.dol.files.txt places texPalette.c,
# so the SDK's GX ends before it and what follows is Konami's own texture and
# debug helpers - an earlier version of this table ran SDK GX to 0x8004A000
# and duly reported `fn_8004701C` as a matchable SDK function when it is in
# texPalette.c. 0x800066F8 is rel_loader.c by the same evidence, so the block
# below 0x80020000 is Metrowerks runtime AND Konami boot code interleaved and
# is marked "partly" for that reason, not as a hedge.
#
# Anywhere a finer attribution is needed, main.dol.files.txt is the authority
# and this table is not.
REGIONS = [
    (0x80005000, 0x80020000, "CodeWarrior runtime / boot",   "partly"),
    (0x80020000, 0x80030000, "SDK: OS",                      "yes"),
    (0x80030000, 0x80040000, "SDK: audio / DSP / AR / CARD", "yes"),
    (0x80040000, 0x80046EF0, "SDK: GX",                      "yes"),
    (0x80046EF0, 0x8004A000, "Konami texture / debug",       "no"),
    (0x8004A000, 0x8004E700, "CR_System (Konami)",           "no"),
    (0x8004E700, 0x80062000, "Konami sound / Tremor",        "no"),
]


def region_of(addr):
    for lo, hi, name, ref in REGIONS:
        if lo <= addr < hi:
            return name, ref
    return "other", "no"


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--symbols", default="config/symbols/main.dol.symbols.txt")
    ap.add_argument("--rel-asm",
                    default="build/phase0/out/mgso_pal/asm/auto_00_00000000_text.s")
    ap.add_argument("--boundaries", default="build/phase0/main.symbols.txt")
    ap.add_argument("--min-calls", type=int, default=3,
                    help="only list functions with at least this many call sites")
    args = ap.parse_args()

    named = set()
    for line in open(args.symbols):
        m = re.match(r"^\.\w+\s+0x([0-9A-Fa-f]+)\s", line)
        if m:
            named.add(int(m.group(1), 16))

    sites = collections.Counter()
    for line in open(args.rel_asm):
        for t in re.findall(r"\bbl fn_(80[0-9A-Fa-f]{6})", line):
            sites[int(t, 16)] += 1

    sizes = {}
    for line in open(args.boundaries):
        m = re.match(r"^\S+ = \.\w+:0x([0-9A-Fa-f]+); // type:function size:0x([0-9A-Fa-f]+)",
                     line)
        if m:
            sizes[int(m.group(1), 16)] = int(m.group(2), 16)

    unnamed = sorted(((c, a) for a, c in sites.items() if a not in named),
                     reverse=True)

    by_region = collections.Counter()
    refs = {}
    for c, a in unnamed:
        name, ref = region_of(a)
        by_region[name] += c
        refs[name] = ref

    total = sum(by_region.values())
    matchable = sum(n for r, n in by_region.items() if refs[r] != "no")
    print(f"{total} unnamed call sites across {len(unnamed)} functions\n")
    print(f"| {'region':<30} | call sites | public reference? |")
    print(f"|{'-'*32}|{'-'*12}|{'-'*19}|")
    for r, n in by_region.most_common():
        print(f"| {r:<30} | {n:>10} | {refs[r]:<17} |")
    print(f"\n{matchable} of {total} ({100.0*matchable/total:.0f}%) are in code "
          f"with a public reference to match against.\n")

    print(f"functions with >= {args.min_calls} call sites, in referenced code:")
    for c, a in unnamed:
        name, ref = region_of(a)
        if c >= args.min_calls and ref != "no":
            print(f"  {c:>4} calls  0x{a:08X}  size 0x{sizes.get(a, 0):X}  {name}")


if __name__ == "__main__":
    main()
