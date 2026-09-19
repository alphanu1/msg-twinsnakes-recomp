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

# A WHOLE function written in assembly: `asm void PSVECAdd(...) { ... }`.
#
# This is how the SDK decomp writes nearly all of its assembly - 146 of them -
# and handling only the inner `asm { }` block form saw 33. That mattered more
# than the count suggests: whole-assembly functions are exactly the small
# leaves that call nothing and are called by nothing, so the call graph cannot
# reach them, and their instruction sequence is the only signature they have.
ASM_FUNC = re.compile(
    r'^\s*(?:static\s+)?asm\s+[\w\*]+\s+\**(\w+)\s*\([^;{]*\)\s*\{(.*?)^\}',
    re.M | re.S)

def opcodes(text, numeric_offsets=True):
    """Opcode mnemonics, the quantised W bit, and optionally memory offsets.

    OFFSETS CANNOT BE COMPARED ACROSS THE TWO SIDES. The SDK decomp writes
    them symbolically - `psq_l f2, Vec.x(a), 0, 0` - because the Metrowerks
    assembler resolves them from the struct; our disassembly has the resolved
    numbers. Including them makes every signature fail to match rather than
    making it stricter.

    What replaces them is the W bit, which IS comparable and carries the
    distinction the offsets were there for.

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
        # DIRECTIVES THAT EMIT NO INSTRUCTION. `nofralloc` tells the
        # Metrowerks compiler not to build a stack frame for this asm
        # function; it produces no code, so it appears on the SDK side of the
        # comparison and can never appear on ours. Leaving it in adds one
        # token to the signature of every function that uses it, and since a
        # match is accepted only on the FULL sequence, every one of them
        # failed - PSMTX44Concat among them, which is byte-identical to ours.
        if line in ('nofralloc', 'entry', 'noreturn'):
            continue
        m = re.match(r'([a-z][a-z0-9_.]*)', line)
        if not m:
            continue
        offs = re.findall(r'(-?(?:0x[0-9A-Fa-f]+|\d+))\s*\(', line)
        tag = m.group(1)

        # The quantised load/store's W BIT, which says whether it moves one
        # value or two. It is the difference between a three-component vector
        # and a four-component quaternion, and those otherwise have identical
        # mnemonic sequences - psq_l, psq_l, ps_add, psq_st twice over. The
        # SDK writes its offsets symbolically (`Vec.x(a)`), so the offsets
        # cannot be compared across the two sides; the W bit can, and it is
        # the operand that actually distinguishes them.
        if tag.startswith('psq_'):
            ops = line.split(',')
            if len(ops) >= 3:
                w = ops[-2].strip()
                if re.fullmatch(r'[01]', w):
                    tag += '/w' + w

        if numeric_offsets and offs:
            tag += '@' + ','.join(str(int(o, 0)) for o in offs)
        out.append(tag)
    return out

def load_sdk(paths):
    sigs = {}
    for p in paths:
        src = open(p, errors='ignore').read()
        bodies = [(n, ASM.findall(b)) for n, b in FUNC.findall(src)]
        bodies += [(n, [b]) for n, b in ASM_FUNC.findall(src)]
        for name, blocks in bodies:
            if not blocks:
                continue
            ops = []
            for b in blocks:
                ops += opcodes(b, numeric_offsets=False)
            # STRIP A TRAILING `blr` HERE TOO. Some SDK asm bodies write
            # the return explicitly and some leave the compiler to add it,
            # and our disassembly always has it. Stripping on one side only
            # made every explicitly-returning function differ by exactly one
            # token from its own machine code - PSMTX44Concat's 65 tokens
            # were otherwise identical to ours, and it still did not match.
            if ops and ops[-1] == 'blr':
                ops = ops[:-1]
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
        m = re.match(r'^/\* \S+ \S+ [0-9A-F ]+\*/\s+(.*)', line)
        if m:
            # Same extraction as the SDK side, and without numeric offsets,
            # so the two signatures are built the same way from both.
            ops += opcodes(m.group(1), numeric_offsets=False)
    return out

def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--sdk', nargs='+', required=True, help='SDK decomp .c files')
    ap.add_argument('--asm', required=True, help='our disassembly (.s)')
    ap.add_argument('--boundaries',
                    help='dtk map of every function and its size, so the '
                         'output carries a size the merge can check against')
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
        # A size of zero is not a placeholder - the merge checks the size
        # against dtk's recovered boundary, and one that does not match means
        # the candidate is not the function it claims to be. Emitting zero
        # makes every match fail that check for the wrong reason.
        bounds = {}
        if a.boundaries:
            for line in open(a.boundaries):
                m = re.match(r'^\S+ = (\.\w+):0x([0-9A-Fa-f]+); '
                             r'// type:function size:0x([0-9A-Fa-f]+)', line)
                if m:
                    bounds[int(m.group(2), 16)] = int(m.group(3), 16)

        with open(a.out, 'w') as f:
            for fn, n in sorted(hits.items()):
                # A function our disassembly already names is a CHECK, not a
                # discovery: the matcher reproducing a name we arrived at by
                # another route is evidence the signature works. It has no
                # address to emit, so it is reported and not written.
                if not re.fullmatch(r'fn_[0-9A-Fa-f]+', fn):
                    continue
                addr = int(fn.split('_')[-1], 16)
                f.write(f".text 0x{addr:08X} 0x{bounds.get(addr, 0):X} "
                        f"{n} sdk2004-asm\n")
        print(f"wrote {a.out}")

if __name__ == '__main__':
    main()
