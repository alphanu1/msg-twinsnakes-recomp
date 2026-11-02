#!/usr/bin/env python3
"""Carry a symbol map from one build of a game to another, by byte matching.

Two regional builds of the same game are compiled from the same source by
the same compiler, so most functions survive byte-for-byte, or within a
few immediate operands of it. That makes the first symbol pass for a new
build mechanical: take every named function in the build we have, find
its bytes in the build we want, and keep only what the evidence supports.

Two strengths of match, both reported, never blended:

  exact   the function's bytes occur exactly once in the target. Nothing
          else in the target looks like it, so the address is proved.
  near    no exact hit, but at the candidate address the bytes differ in
          at most 1/8 of their words (address immediates and branch
          displacements are what legitimately changes between builds).
          The candidate is either a fixed section delta collected from
          the exact hits, or the same address. Still evidence, weaker,
          and recorded as such in the origin column.

Anything else is NOT transferred. A name at a guessed address is worse
than no name: the patch table would intercept the wrong code and the
failure would be silent. Unmatched symbols are listed, not placed.

The origin of every transferred symbol is rewritten to record that this
is how it was produced: 'byte-transfer' (exact) or 'byte-transfer-near'.

usage: transfer-symbols.py <symbols.txt> <from.dol> <to.dol> [-o <out>]
"""
import re, struct, sys
from collections import Counter

def dol_sections(path):
    d = open(path, 'rb').read()
    secs = []
    for i in range(18):
        off = struct.unpack_from('>I', d, i * 4)[0]
        addr = struct.unpack_from('>I', d, 0x48 + i * 4)[0]
        size = struct.unpack_from('>I', d, 0x90 + i * 4)[0]
        if size:
            secs.append((addr, off, size))
    return d, secs

def read_at(d, secs, addr, size):
    for a, o, s in secs:
        if a <= addr and addr + size <= a + s:
            return d[o + (addr - a): o + (addr - a) + size]
    return None

def offset_to_addr(secs, off):
    for a, o, s in secs:
        if o <= off < o + s:
            return a + (off - o)
    return None

def main():
    args = sys.argv[1:]
    out = None
    if '-o' in args:
        out = args[args.index('-o') + 1]
        del args[args.index('-o'):args.index('-o') + 2]
    if len(args) != 3:
        print(__doc__)
        return 2
    symbols_path, from_path, to_path = args
    fd, fs = dol_sections(from_path)
    td, ts = dol_sections(to_path)

    entries, header = [], []
    for line in open(symbols_path):
        if line.startswith('#') or not line.strip():
            header.append(line)
            continue
        m = re.match(r'^(\.\w+) 0x([0-9A-Fa-f]+) (\S+) (\S+) (\S+)', line)
        if m:
            try: size = int(m.group(3), 16)
            except ValueError: size = 0
            entries.append((m.group(1), int(m.group(2), 16), size,
                            m.group(3), m.group(4), m.group(5)))

    placed, exact_deltas = {}, Counter()
    for sec, addr, size, size_s, name, origin in entries:
        body = read_at(fd, fs, addr, size) if 8 <= size <= 65536 else None
        if body is None:
            continue
        first = td.find(body)
        if first >= 0 and td.find(body, first + 1) < 0:
            va = offset_to_addr(ts, first)
            if va is not None:
                placed[name] = (sec, va, size_s, 'byte-transfer')
                exact_deltas[va - addr] += 1

    for sec, addr, size, size_s, name, origin in entries:
        if name in placed or not (8 <= size <= 65536):
            continue
        body = read_at(fd, fs, addr, size)
        if body is None:
            continue
        for delta, _ in exact_deltas.most_common(4):
            va = addr + delta
            cand = read_at(td, ts, va, size)
            if cand is None:
                continue
            words = size // 4
            diff = sum(1 for i in range(words)
                       if body[i * 4:i * 4 + 4] != cand[i * 4:i * 4 + 4])
            if diff <= max(2, words // 8):
                placed[name] = (sec, va, size_s, 'byte-transfer-near')
                break

    lines = [h for h in header if 'GGSPA4' not in h]
    lines.append('# Transferred by tools/transfer-symbols.py - see its docstring.\n')
    for sec, addr, size, size_s, name, origin in entries:
        if name in placed:
            psec, va, psize, porigin = placed[name]
            lines.append(f'{psec} 0x{va:08X} {psize} {name} {porigin}\n')
    text = ''.join(lines)
    if out:
        open(out, 'w').write(text)
    else:
        sys.stdout.write(text)
    total = len([e for e in entries if 8 <= e[2] <= 65536])
    exact = sum(1 for v in placed.values() if v[3] == 'byte-transfer')
    near = sum(1 for v in placed.values() if v[3] == 'byte-transfer-near')
    print(f'{len(placed)}/{total} symbols placed ({exact} exact, {near} near)',
          file=sys.stderr)
    return 0

if __name__ == '__main__':
    sys.exit(main())
