#!/usr/bin/env python3
"""Name SDK functions by matching their inline-assembly bodies.

Stage 5b of docs/decompilation-process.md.

Parts of the Dolphin SDK - the paired-single maths in particular - are written
as inline assembly rather than C. That makes them the strongest signature
available: the compiler emits those instructions verbatim, so the machine code
is identical across SDK revisions, link orders and games. Unlike the ordered
run alignment in stage 5, this needs no anchors and no assumptions about what
either binary links.

A match is accepted only on the FULL opcode sequence, in order, with the same
length. A partial or prefix match is not a match.

Names come from a public clean-room decompilation; the confirmation that a name
belongs at an address comes from our own binary.
"""
import re, sys, argparse, collections

# an asm { ... } body inside a named function
FUNC = re.compile(
    r'^\s*(?:static\s+)?[\w\*]+\s+\**(\w+)\s*\([^;{]*\)\s*\{(.*?)^\}',
    re.M | re.S)
ASM = re.compile(r'asm\s*\{(.*?)\}', re.S)

def opcodes(text):
    """Opcode mnemonics PLUS memory offsets, in order.

    Mnemonics alone are not a signature. PSVECDotProduct and
    PSQUATDotProduct have byte-identical mnemonic sequences - psq_l, psq_l,
    ps_mul, psq_l, psq_l, ps_madd, ps_sum0 - and differ only in their
    offsets: the quaternion version loads at 0 and 8 (four components), the
    vector version at 0 and 4, overlapping, to reach three. Matching on
    mnemonics alone named one of them wrong.
    """
    out = []
    for line in text.splitlines():
        line = re.sub(r'(//|/\*).*', '', line).strip()
        if not line or line.endswith(':') or line.startswith('#'):
            continue
        m = re.match(r'([a-z][a-z0-9_.]*)', line)
        if not m:
            continue
        offs = re.findall(r'(-?(?:0x[0-9A-Fa-f]+|\d+))\s*\(', line)
        out.append(m.group(1) + ('@' + ','.join(str(int(o, 0)) for o in offs) if offs else ''))
    return out

def load_sdk(paths):
    sigs = {}
    for p in paths:
        src = open(p, errors='ignore').read()
        for name, body in FUNC.findall(src):
            blocks = ASM.findall(body)
            if not blocks:
                continue
            ops = []
            for b in blocks:
                ops += opcodes(b)
            if len(ops) >= 4:                 # too short to be distinctive
                sigs.setdefault(tuple(ops), []).append(name)
    return sigs

def load_ours(path):
    """Disassembly -> {address: (name, [opcodes])}."""
    out, cur, ops = {}, None, []
    for line in open(path):
        m = re.match(r'^\.fn (\S+?), ', line)
        if m:
            cur, ops = m.group(1), []
            continue
        if line.startswith('.endfn'):
            if cur:
                out[cur] = ops
            cur, ops = None, []
            continue
        if cur is None:
            continue
        m = re.match(r'^/\* \S+ \S+ [0-9A-F ]+\*/\s+([a-z][a-z0-9_.]*)(.*)', line)
        if m:
            offs = re.findall(r'(-?(?:0x[0-9A-Fa-f]+|\d+))\s*\(', m.group(2))
            ops.append(m.group(1) + ('@' + ','.join(str(int(o, 0)) for o in offs) if offs else ''))
    return out

def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--sdk', nargs='+', required=True, help='SDK decomp .c files')
    ap.add_argument('--asm', required=True, help='our disassembly (.s)')
    ap.add_argument('--out')
    a = ap.parse_args()

    sigs = load_sdk(a.sdk)
    ours = load_ours(a.asm)
    print(f"SDK inline-asm signatures  {len(sigs):>5}")
    print(f"our functions              {len(ours):>5}")

    hits, ambiguous = {}, 0
    for fn, ops in ours.items():
        body = tuple(ops[:-1]) if ops and ops[-1] == 'blr' else tuple(ops)
        names = sigs.get(body)
        if not names:
            continue
        if len(set(names)) > 1:
            ambiguous += 1
            continue
        hits[fn] = names[0]

    # A body that several of OUR functions share is not a signature. The SDK
    # has distinct functions with identical instruction sequences - a 4-float
    # add is a 4-float add - so matching one name to two addresses means the
    # sequence does not identify the function. Refuse both rather than guess.
    byname = collections.Counter(hits.values())
    collided = {fn: n for fn, n in hits.items() if byname[n] > 1}
    hits = {fn: n for fn, n in hits.items() if byname[n] == 1}

    print(f"exact full-sequence matches{len(hits):>5}")
    print(f"ambiguous signature (skip) {ambiguous:>5}")
    print(f"non-unique body (refused)  {len(collided):>5}"
          + (f"  {sorted(collided)}" if collided else ""))
    for fn, n in sorted(hits.items()):
        print(f"  {fn} -> {n}")
    if a.out and hits:
        with open(a.out, 'w') as f:
            for fn, n in sorted(hits.items()):
                addr = fn.split('_')[-1]
                f.write(f".text 0x{addr.upper()} 0x0 {n} sdk2004-asm\n")
        print(f"wrote {a.out}")

if __name__ == '__main__':
    main()
