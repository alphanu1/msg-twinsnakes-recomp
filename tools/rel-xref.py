#!/usr/bin/env python3
"""Who reaches this REL address - by `bl`, and by POINTER.

WHY BOTH. This engine reaches most of its work through function pointers:
tasks from a table, handlers registered at construction, states from a jump
table. Searching for `bl` alone answers "nothing calls this", which is true
and useless - the movie's director is reached by neither a call nor a task
entry, but by a `lis`/`addi` pair inside the function that registers it
(HANDOFF F267).

So this reports both, and for pointers it uses the REL's own relocation
table rather than scanning for a constant: a REL stores its sections
unrelocated, so the address simply is not in the file to find. The
relocations are where it is written down.

usage: rel-xref.py <module.rel> <guest-addr> [--rel-base 0x7F008000]
"""
import struct, sys

def load(path, base):
    d = open(path, 'rb').read()
    (id_, nxt, prv, nsec, secoff, nameoff, namesz, ver,
     bsssize, reloff, impoff, impsize) = struct.unpack('>12I', d[0:48])
    secs = []
    for i in range(nsec):
        o, l = struct.unpack('>2I', d[secoff + i * 8:secoff + i * 8 + 8])
        secs.append((o & ~3, l, o & 1))
    imp = [struct.unpack('>2I', d[impoff + i * 8:impoff + i * 8 + 8])
           for i in range(impsize // 8)]
    return d, id_, secs, imp

def main():
    args = [a for a in sys.argv[1:] if not a.startswith('--')]
    base = 0x7F008000
    for i, a in enumerate(sys.argv):
        if a == '--rel-base':
            base = int(sys.argv[i + 1], 0)
            args = [x for x in args if x != sys.argv[i + 1]]
    if len(args) < 2:
        print(__doc__); return 2
    d, id_, secs, imp = load(args[0], base)
    target = int(args[1], 0)

    # which section, and the offset within it
    tsec = toff = None
    for i, (o, l, e) in enumerate(secs):
        if o and o <= (target - base) < o + l:
            tsec, toff = i, target - (base + o)
    if tsec is None:
        print(f"0x{target:08X} is in no loaded section"); return 1

    calls = []
    for o, l, e in secs:
        if not e: continue
        for k in range(0, l - 3, 4):
            w = struct.unpack('>I', d[o + k:o + k + 4])[0]
            if (w >> 26) != 18 or not (w & 1): continue
            li = w & 0x03FFFFFC
            if li & 0x02000000: li -= 0x04000000
            a = base + o + k
            if a + li == target: calls.append(a)

    ptrs = []
    for m, r in imp:
        if m != id_: continue
        off, sec, pos = r, 0, 0
        while True:
            o, t, s, a = struct.unpack('>HBBI', d[off:off + 8]); off += 8
            if t == 203: break
            if t == 202: sec = s; pos = 0; continue
            pos += o
            if s == tsec and a == toff:
                ptrs.append((sec, pos, t))

    print(f"0x{target:08X}  =  section {tsec} + 0x{toff:06X}")
    print(f"  bl callers: {len(calls)}")
    for a in calls: print(f"    0x{a:08X}")
    print(f"  pointers (from the relocation table): {len(ptrs)}")
    for sec, pos, t in ptrs:
        g = base + secs[sec][0] + pos
        print(f"    0x{g:08X}  in section {sec} "
              f"({'CODE' if secs[sec][2] else 'data'})  reloc type {t}")
    return 0

if __name__ == '__main__':
    sys.exit(main())
