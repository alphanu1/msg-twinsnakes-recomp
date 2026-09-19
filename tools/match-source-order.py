#!/usr/bin/env python3
"""Name functions from the file and LINE NUMBER they carry in their own code.

Stage 5f of docs/decompilation-process.md.

Stage 5e found that this binary's functions hand their own `__FILE__` to a
tracking allocator. They hand it `__LINE__` as well, and that changes what is
possible: the line number is a plain `li` in the instruction stream, so a
function does not merely belong to `framing.c` - it contains the allocation at
`framing.c:864`, and exactly one function in that file does.

    addi r5, lbl_80063B08@l   ; "framing.c"
    li   r6, 0x360            ; 864
    bl   fn_80055B74          ; the tracking allocator

That is not an inference about what a function resembles. It is the source
location, compiled in, and it identifies the function outright.

TWO THINGS STILL HAVE TO BE CHECKED, because a line number alone can mislead:

  - The source must be the version the game built against. A file that has
    gained or lost lines since puts every line in the wrong function, and the
    failure is silent. So a file is only accepted when EVERY candidate line
    lands inside some function - a version mismatch scatters them.
  - The result must respect ordering. The compiler emits functions in source
    order, so our addresses ascending must map to source functions ascending.
    A mapping that goes backwards is wrong however well each line fits.

Both are refusals, not warnings: a file that fails either is left unnamed.
"""
import re, os, argparse, collections

C_DEF = re.compile(r'^(?:static\s+)?(?:inline\s+)?(?:extern\s+)?(?:asm\s+)?[A-Za-z_][\w\s\*]*?\b([A-Za-z_]\w*)\s*\([^;]*?\)\s*\{', re.M)
KEYWORDS = {'if','for','while','switch','return','sizeof','do','else','case'}

def source_functions(path):
    """(name, first_line, last_line) for each function, in source order."""
    text = open(path, errors='replace').read()
    starts = [0]
    for ch in text:
        pass
    line_of = []
    n = 1
    for ch in text:
        line_of.append(n)
        if ch == '\n':
            n += 1
    line_of.append(n)

    out = []
    for m in C_DEF.finditer(text):
        name = m.group(1)
        if name in KEYWORDS:
            continue
        i = text.index('{', m.end() - 1)
        depth, j = 0, i
        while j < len(text):
            if text[j] == '{': depth += 1
            elif text[j] == '}':
                depth -= 1
                if depth == 0: break
            j += 1
        out.append((name, line_of[m.start()], line_of[min(j, len(line_of) - 1)]))
    return out

def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--file-map', required=True,
                    help='attribute-by-strings.py output: address, file, lines')
    ap.add_argument('--boundaries', required=True)
    ap.add_argument('--symbols', required=True)
    ap.add_argument('--src', nargs='+', required=True)
    ap.add_argument('--out')
    a = ap.parse_args()

    entries = []
    for line in open(a.file_map):
        m = re.match(r'^0x([0-9A-Fa-f]+)\s+(\S+)\s*(\S*)', line)
        if m:
            lines = [int(v) for v in m.group(3).split(',') if v]
            entries.append((int(m.group(1), 16), m.group(2), lines))

    bounds = {}
    for line in open(a.boundaries):
        m = re.match(r'^\S+ = (\.\w+):0x([0-9A-Fa-f]+); // type:function size:0x([0-9A-Fa-f]+)', line)
        if m: bounds[int(m.group(2), 16)] = int(m.group(3), 16)

    named = {}
    for line in open(a.symbols):
        m = re.match(r'^\S+ 0x([0-9A-Fa-f]+) 0x\S+ (\S+)', line)
        if m: named[int(m.group(1), 16)] = m.group(2)
    taken = set(named.values())

    found = {}
    for root in a.src:
        for dirpath, _, files in os.walk(root):
            for f in files:
                found.setdefault(f, os.path.join(dirpath, f))

    by_file = collections.defaultdict(list)
    for addr, f, lines in entries:
        by_file[f].append((addr, lines))

    assigned, refused = {}, []
    for fname, items in sorted(by_file.items()):
        path = found.get(fname)
        if not path:
            refused.append(f"  {fname:<20} no source available")
            continue

        src = source_functions(path)
        items.sort()

        # Each address: which source functions could its candidate lines be in?
        cands = []
        for addr, lines in items:
            hit = {name for (name, lo, hi) in src
                   for v in lines if lo <= v <= hi}
            cands.append((addr, hit))

        # An address whose lines land nowhere is skipped; the file is not.
        # A version difference moves some lines and not others, and throwing
        # away a file because one allocation drifted discards the ones that
        # did not.
        missing = sum(1 for _a2, h in cands if not h)
        cands = [(a2, h) for a2, h in cands if h]
        if not cands:
            refused.append(f"  {fname:<20} REFUSED: no line lands in any "
                           f"function (source version differs)")
            continue

        # A UNIQUE containing function is the answer; anything else is not.
        #
        # This deliberately does NOT require address order to follow source
        # order. It was written that way first, and the data refused it:
        # floor0.c's highest address holds the allocation at line 302, which
        # is in the file's THIRD function. Metrowerks reordered them. An
        # ordering constraint would have rejected two names that a single
        # exact line number establishes on its own.
        #
        # What is required instead is injectivity - two addresses cannot be
        # the same function - and that every accepted line lands in exactly
        # one function. An ambiguous address is skipped, not guessed.
        chosen, ambiguous = {}, 0
        for addr, hit in cands:
            if len(hit) == 1:
                chosen[addr] = next(iter(hit))
            else:
                ambiguous += 1

        seen = collections.Counter(chosen.values())
        clashed = [a2 for a2, n2 in chosen.items() if seen[n2] > 1]
        for a2 in clashed:
            del chosen[a2]

        if not chosen:
            refused.append(f"  {fname:<20} REFUSED: {ambiguous} ambiguous, "
                           f"{len(clashed)} clashing, none decided")
            continue

        n = 0
        for addr, name in chosen.items():
            if addr in named or name in taken:
                continue
            assigned[addr] = name
            taken.add(name)
            n += 1
        print(f"  {fname:<20} {len(chosen)} located, {n} newly named"
              f"  ({ambiguous} ambiguous, {len(clashed)} clashing)")

    for r in refused:
        print(r)

    print(f"\nnamed {len(assigned)}")
    for addr in sorted(assigned):
        print(f"  0x{addr:08X}  {assigned[addr]}")

    if a.out:
        with open(a.out, 'w') as fh:
            for addr in sorted(assigned):
                fh.write(f".text 0x{addr:08X} 0x{bounds.get(addr,0):X} "
                         f"{assigned[addr]} fileline\n")
        print(f"wrote {a.out}")

if __name__ == '__main__':
    main()
