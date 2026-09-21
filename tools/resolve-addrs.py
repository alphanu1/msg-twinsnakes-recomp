#!/usr/bin/env python3
"""Put names to raw guest addresses.

The host prints addresses, never names: it has no symbol table, and giving it
one would mean keeping a second copy of config/symbols in step with the first.
So resolution happens here, which also means an old profile dump or crash log
can be re-resolved as naming improves, rather than being frozen at whatever
was known when it was produced.

Two address spaces:

  0x8000_0000..  main.dol, loaded at its link address.
  0x7F00_80EC..  the overlay, mgso_pal.rel, in the second address window.
                 Its symbols are recorded as offsets into .text, because the
                 load address is the game's choice and has already changed
                 once; --rel-base overrides where .text was actually put.

Usage:  resolve-addrs.py [--rel-base 0x7E...] [file ...]
Reads stdin when given no files. Any 0x-prefixed address in the text is
annotated in place, so it works on profile dumps, crash paths and pasted logs
alike.
"""
import bisect
import os
import re
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))


def load(path, bias=0):
    """Read a symbol file into sorted (address, size, name) triples.

    Size is '?' for symbols whose extent we never established - a name with
    no size can still name its own entry point, but it must not be allowed to
    claim every address after it, which is how `main+0x828` came to be printed
    for an address in a completely different function.
    """
    out = []
    try:
        fh = open(path)
    except OSError:
        return out
    with fh:
        for line in fh:
            # Comments first. The five-column pattern below is loose enough
            # to match prose - a comment mentioning an address and a couple
            # of words parses as a symbol and poisons the table - so they are
            # discarded before anything tries to read them as data.
            if line.lstrip().startswith('#') or not line.strip():
                continue
            m = re.match(r'^\s*(\.\w+)\s+0x([0-9A-Fa-f]+)\s+'
                         r'(0x[0-9A-Fa-f]+|\?)\s+(\S+)\s+(\S+)\s*$', line)
            if not m:
                continue
            size = m.group(3)
            out.append((int(m.group(2), 16) + bias,
                        -1 if size == '?' else int(size, 16),
                        m.group(4)))
    out.sort(key=lambda t: t[0])
    return out


def load_files(path, bias=0):
    """Read a file-attribution table into sorted (address, size, file) triples.

    A WEAKER ANSWER THAN A NAME, AND WORTH PRINTING ANYWAY.

    The engine overlay is Konami's own code with no public decompilation, so
    most of it will never have a name. But a function that hands its own
    __FILE__ to a panic routine says which source file it was compiled from,
    and for reading a profile that is most of the value: "38% of the run is
    in mpegGCN.c" is an answer, where a column of bare addresses is not.

    Kept distinct from real names by the caller, which brackets these, so a
    file attribution can never be mistaken for a symbol.
    """
    out = []
    try:
        fh = open(path)
    except OSError:
        return out
    with fh:
        for line in fh:
            if line.lstrip().startswith('#') or not line.strip():
                continue
            m = re.match(r'^\s*0x([0-9A-Fa-f]+)\s+(0x[0-9A-Fa-f]+|\?)\s+'
                         r'(\S+\.c)\b', line)
            if not m:
                continue
            size = m.group(2)
            out.append((int(m.group(1), 16) + bias,
                        -1 if size == '?' else int(size, 16),
                        m.group(3)))
    out.sort(key=lambda t: t[0])
    return out


def resolve(table, keys, addr):
    i = bisect.bisect_right(keys, addr) - 1
    if i < 0:
        return None
    start, size, name = table[i]
    if addr == start:
        return name
    # Without a size, only the exact entry point may be claimed. Guessing
    # produces confident nonsense, which is worse than an unnamed address.
    if size < 0:
        return None
    if addr >= start + size:
        return None
    return '%s+0x%X' % (name, addr - start)


def main():
    args = sys.argv[1:]
    rel_base = 0x7F0080EC
    if '--rel-base' in args:
        i = args.index('--rel-base')
        rel_base = int(args[i + 1], 0)
        del args[i:i + 2]

    dol = load(os.path.join(ROOT, 'config/symbols/main.dol.symbols.txt'))
    rel = load(os.path.join(ROOT, 'config/symbols/mgso_pal.rel.symbols.txt'),
               bias=rel_base)
    dol_f = load_files(os.path.join(ROOT, 'config/symbols/main.dol.files.txt'))
    rel_f = load_files(os.path.join(ROOT, 'config/symbols/mgso_pal.rel.files.txt'),
                       bias=rel_base)
    dol_k = [t[0] for t in dol]
    rel_k = [t[0] for t in rel]
    dol_fk = [t[0] for t in dol_f]
    rel_fk = [t[0] for t in rel_f]

    def name_for(addr):
        """A name if one is known, else the source file, else nothing.

        The file is bracketed so the two can never be confused when this
        output is read back or pasted into a note: `OSGetTime` is a name,
        `[mpegGCN.c+0x278]` is an attribution and no claim about a name.
        """
        if addr >= 0x80000000:
            nm = resolve(dol, dol_k, addr)
            if nm:
                return nm
            f = resolve(dol_f, dol_fk, addr)
            return '[%s]' % f if f else None
        if addr >= 0x7E000000:
            nm = resolve(rel, rel_k, addr)
            if nm:
                return nm
            f = resolve(rel_f, rel_fk, addr)
            return '[%s]' % f if f else None
        return None

    streams = [open(a) for a in args] if args else [sys.stdin]
    for stream in streams:
        for line in stream:
            def sub(m):
                addr = int(m.group(0), 16)
                nm = name_for(addr)
                return m.group(0) + ('  ' + nm if nm else '')
            sys.stdout.write(re.sub(r'0x[0-9A-Fa-f]{8}\b', sub, line.rstrip()) + '\n')


if __name__ == '__main__':
    main()
