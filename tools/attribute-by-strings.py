#!/usr/bin/env python3
"""Attribute functions to their source file, from the strings they reference.

Stage 5e of docs/decompilation-process.md.

A great deal of shipped C carries its own source file name. Assertions embed
`__FILE__`, error handlers print it, and the SDK's `OSPanic` takes it as an
argument - so `main.dol` contains "dvdfs.c", "texPalette.c", "vorbisfile.c"
and a dozen more, each sitting in read-only data with the functions that use
it pointing straight at it.

That pointer is evidence of a kind nothing else here provides. Ordered
alignment (stage 5) says "this function is where MKDD puts OSGetTime"; a call
graph (stage 5d) says "this function calls what OSFatal calls". This says
something simpler and harder to argue with: **this function is compiled from
this file**, because it holds that file's name and hands it to a panic
routine.

What it gives is a FILE, not a name. That is still worth a great deal:

  - It bounds the call-graph matcher's candidates to one file's functions,
    turning many ambiguous matches into unique ones.
  - It is an independent check on names produced any other way.
  - It identifies whole third-party libraries. This binary contains
    libvorbis - `floor0.c`, `codebook.c`, `framing.c`, `sharedbook.c` - which
    is public BSD-licensed code whose function set is known.

The reference is `lis rX, sym@ha` followed by `addi rX, rX, sym@l`, the
PowerPC way of forming a 32-bit address, and dtk has already labelled the
target. So the work is to read the disassembly, resolve each label to an
address, and check whether that address holds a file name.
"""
import re, sys, os, struct, argparse, collections

FILE_STRING = re.compile(rb'^[A-Za-z0-9_./\-]+\.(?:c|cpp|cc|h)$')

def dol_sections(path):
    d = open(path, 'rb').read()
    u32 = lambda o: struct.unpack('>I', d[o:o + 4])[0]
    secs = []
    for i in range(18):
        off, addr, size = u32(i * 4), u32(0x48 + i * 4), u32(0x90 + i * 4)
        if size:
            secs.append((off, addr, size))
    return d, secs

def to_vaddr(secs, off):
    for o, a, s in secs:
        if o <= off < o + s:
            return a + (off - o)
    return None

def find_file_strings(path):
    """virtual address -> source file name, for every file name in the DOL."""
    d, secs = dol_sections(path)
    out = {}
    start = None
    for i, b in enumerate(d):
        if 0x20 <= b < 0x7F:
            if start is None:
                start = i
        else:
            if start is not None and b == 0 and i - start >= 4:
                tok = d[start:i]
                if FILE_STRING.match(tok):
                    va = to_vaddr(secs, start)
                    if va:
                        out[va] = tok.decode()
            start = None
    return out

def labels(asm_paths):
    """dtk label name -> address, for every lbl_/jumptable it emits."""
    out = {}
    pat = re.compile(r'\b(lbl_|jumptable_|fn_|data_|rodata_)?([0-9A-Fa-f]{8})\b')
    for p in asm_paths:
        for line in open(p, errors='replace'):
            m = re.match(r'^\s*\.(?:obj|sym|global)\s+(\S+?),?\s', line)
            if m:
                mm = re.search(r'([0-9A-Fa-f]{8})', m.group(1))
                if mm:
                    out[m.group(1)] = int(mm.group(1), 16)
    return out

def function_references(asm_paths):
    """function address -> (referenced addresses, immediate constants).

    Both halves of a `lis`/`addi` pair name the same symbol, so either half
    alone is enough to know what is referenced - which matters because the
    compiler routinely splits them across unrelated instructions.

    The IMMEDIATES matter as much as the references. A function that hands its
    file name to a tracking allocator hands it `__LINE__` in the next
    argument, so the line number is sitting in the instruction stream as a
    plain `li`. That is not a hint about which function this is - it is the
    exact source line, and it identifies the function outright.
    """
    refs = collections.defaultdict(set)
    imms = collections.defaultdict(set)
    fn_start = re.compile(r'^\s*\.fn\s+(?:fn_)?[0-9A-Za-z_]*?(8[0-9A-Fa-f]{7})\b')
    sym_ref = re.compile(r'\b(?:lbl|data|rodata|jumptable|fn)_(8[0-9A-Fa-f]{7})@(?:ha|l|sda21)')
    li_imm = re.compile(r'\bli\s+r\d+,\s*(-?0x[0-9A-Fa-f]+|-?\d+)\s*$')
    cur = None

    # A CALL'S ARGUMENT SETUP, not a whole function.
    #
    # `__LINE__` is one argument of the allocator call that `__FILE__` is
    # also an argument of, so the two are set up within a few instructions of
    # each other and the call ends the group. Taking every immediate in the
    # function instead sweeps up loop bounds, flags and structure sizes - and
    # a spurious small constant that happens to land inside an early function
    # blocks the real line from being used, which is exactly how this first
    # failed.
    for p in asm_paths:
        window = []          # (symbol addresses, immediates) since the last bl
        for line in open(p, errors='replace'):
            m = fn_start.match(line)
            if m:
                cur = int(m.group(1), 16)
                window = []
                continue
            if cur is None:
                continue

            syms = [int(mm.group(1), 16) for mm in sym_ref.finditer(line)]
            for a2 in syms:
                refs[cur].add(a2)

            mm = li_imm.search(line.rstrip())
            val = None
            if mm:
                v = int(mm.group(1), 0)
                if 0 < v < 30000:
                    val = v

            window.append((syms, val))

            if re.search(r'\bbl\s+\S+\s*$', line.rstrip()):
                # The group is closed. If a file-name string was referenced in
                # it, every immediate in the same group is a line candidate.
                group_syms = [a2 for syms2, _ in window for a2 in syms2]
                group_imms = [v for _, v in window if v is not None]
                if group_syms and group_imms:
                    for a2 in group_syms:
                        imms.setdefault((cur, a2), set()).update(group_imms)
                window = []
    return refs, imms

def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--dol', required=True)
    ap.add_argument('--asm', nargs='+', required=True)
    ap.add_argument('--symbols')
    ap.add_argument('--out')
    a = ap.parse_args()

    strings = find_file_strings(a.dol)
    print(f"source file names in the binary: {len(strings)}")
    for va, name in sorted(strings.items()):
        print(f"  0x{va:08X}  {name}")

    refs, grouped = function_references(a.asm)
    # Immediates that shared an argument setup with a file-name string.
    imms = collections.defaultdict(set)
    for (fn, target), vals in grouped.items():
        imms[fn] |= vals if target in strings else set()
    print(f"\nfunctions with symbol references: {len(refs)}")

    named = {}
    if a.symbols:
        for line in open(a.symbols):
            m = re.match(r'^\S+ 0x([0-9A-Fa-f]+) 0x\S+ (\S+)', line)
            if m:
                named[int(m.group(1), 16)] = m.group(2)

    # A function that references a file-name string is compiled from it.
    # A function referencing TWO is an inlined call across files, and is
    # reported rather than silently attributed to the first.
    attributed = {}
    multi = 0
    for fn, targets in refs.items():
        hits = {strings[t] for t in targets if t in strings}
        if len(hits) == 1:
            attributed[fn] = hits.pop()
        elif len(hits) > 1:
            multi += 1

    print(f"\nattributed to one file : {len(attributed)}")
    print(f"reference two or more  : {multi} (inlining across files; not attributed)")

    by_file = collections.Counter(attributed.values())
    print("\nfunctions per source file (unnamed / total):")
    for f, n in by_file.most_common():
        unnamed = sum(1 for fn, ff in attributed.items()
                      if ff == f and fn not in named)
        print(f"  {f:<24} {unnamed:>4} / {n}")

    if a.out:
        with open(a.out, 'w') as fh:
            for fn in sorted(attributed):
                lines = ','.join(str(v) for v in sorted(imms.get(fn, ())))
                fh.write(f"0x{fn:08X} {attributed[fn]} {lines}\n")
        print(f"\nwrote {a.out}")

if __name__ == '__main__':
    main()
