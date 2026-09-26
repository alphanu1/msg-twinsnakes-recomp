#!/usr/bin/env python3
"""Every function range of a DolRecomp generation, from its dispatch header.

For DOLRECOMP_EXTERNAL_RANGES: the engine (REL) is generated knowing the
DOL's functions, so its calls into the SDK become direct native calls
instead of two trips through the host's run loop (HANDOFF F386).

usage: dol-ranges.py <generated/generated.h> > ranges.txt   ("start end" hex)
"""
import re, sys

text = open(sys.argv[1]).read()
out = []
for m in re.finditer(r'address >= 0x([0-9A-Fa-f]+)u && address < 0x([0-9A-Fa-f]+)u'
                     r'[^\n]*return func_([0-9A-Fa-f]+);', text):
    a, b, name = int(m.group(1), 16), int(m.group(2), 16), int(m.group(3), 16)
    if a == name:
        out.append((a, b))
for m in re.finditer(r'u32 offset = address - 0x([0-9A-Fa-f]+)u;\s*'
                     r'if \(offset < 0x([0-9A-Fa-f]+)u[^{]*\{\s*'
                     r'static const DolRecompFunction chunk_functions\[\] = \{([^}]*)\}', text):
    base, size = int(m.group(1), 16), int(m.group(2), 16)
    starts = [int(x, 16) for x in re.findall(r'func_([0-9A-Fa-f]+)', m.group(3))]
    for i, s in enumerate(starts):
        e = starts[i + 1] if i + 1 < len(starts) else base + size
        out.append((s, e))
for a, b in sorted(set(out)):
    print('0x%08X 0x%08X' % (a, b))
