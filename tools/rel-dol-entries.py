#!/usr/bin/env python3
"""Every main.dol address a REL refers to: the entries the DOL's native code
must have for the REL to reach it.

WHY. A native build enters a function only where its generator knew code
could be entered - function starts, branch targets, addresses held in data.
The generator sees one binary at a time, so what the ENGINE calls in the DOL
is invisible when the DOL is built. Most of it is function starts anyway;
what is not is the compiler's register save and restore helpers
(`_savegpr_N`/`_restgpr_N`), which callers enter part-way, at the first
register they need. The engine enters `_savegpr_27`, the DOL itself never
does, and the native DOL had no entry there (HANDOFF F370).

A REL writes each such reference down as a relocation against module 0 -
the DOL - whose addend is the absolute address. So the list is exact, and
the REL's own contents are not needed, only its relocation table.

Output: one address per line, for DolRecomp's DOLRECOMP_EXTRA_ENTRIES.

usage: rel-dol-entries.py <module.rel> [-o <file>]
"""
import struct, sys

R_DOLPHIN_NOP, R_DOLPHIN_SECTION, R_DOLPHIN_END = 201, 202, 203
ADDRESS_TYPES = set(range(1, 12))    # R_PPC_ADDR32 .. R_PPC_REL14


def dol_refs(path):
    d = open(path, 'rb').read()
    impoff, impsize = struct.unpack('>2I', d[0x28:0x30])
    refs = set()
    for i in range(impsize // 8):
        module, reloff = struct.unpack('>2I', d[impoff + i * 8:impoff + i * 8 + 8])
        if module != 0:
            continue
        p = reloff
        while True:
            _offset, rtype, _section, addend = struct.unpack('>HBBI', d[p:p + 8])
            p += 8
            if rtype == R_DOLPHIN_END:
                break
            if rtype in ADDRESS_TYPES and addend % 4 == 0:
                refs.add(addend)
    return sorted(refs)


def main():
    args = sys.argv[1:]
    if not args:
        print(__doc__)
        return 2
    out = None
    if '-o' in args:
        out = args[args.index('-o') + 1]
    refs = dol_refs(args[0])
    text = ''.join('0x%08X\n' % a for a in refs)
    if out:
        open(out, 'w').write(text)
        print('%d DOL addresses referenced by %s -> %s' % (len(refs), args[0], out))
    else:
        sys.stdout.write(text)
    return 0


if __name__ == '__main__':
    sys.exit(main())
