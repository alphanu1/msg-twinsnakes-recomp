#!/usr/bin/env python3
"""Find a REL function or global inside a snapshot of Dolphin's guest RAM.

WHY THIS EXISTS. Dolphin's OSLink puts the REL wherever its heap happens to
have room, which is not where our loader puts it, so no address derived on
our side can be looked up in the emulator directly. Every comparison against
the oracle needs a translation, and guessing one costs a run.

HOW IT WORKS. Most of a function's machine code is IDENTICAL wherever the
module lands: only the fields OSLink rewrites differ - the 16-bit halves of
a `lis`/`addi` pair building an address, and the displacement of a `bl`.
Those fields are masked out and the rest is searched for. The disassembly
carries the raw bytes per instruction and the operand text says which ones
are relocated, so the mask is DERIVED rather than written by hand.

Finding a global is then exact rather than inferred: locate a function that
references it, read the relocated `lis`/`addi` immediates back out of the
snapshot, and reassemble the address the linker actually wrote.
"""
import re
import struct
import sys

MEM1 = 0x80000000
LINE = re.compile(r'^/\* ([0-9A-F]{8}) [0-9A-F]{8}  ((?:[0-9A-F]{2} ){4})\*/\t(.*)$')


def parse(path):
    """functions: name -> (module offset of entry, [(bytes, operand text)])."""
    functions, name, body, entry = {}, None, [], None
    for line in open(path):
        line = line.rstrip('\n')
        if line.startswith('.fn '):
            name, body, entry = line[4:].split(',')[0], [], None
            continue
        if line.startswith('.endfn'):
            if name and body:
                functions[name] = (entry, body)
            name = None
            continue
        m = LINE.match(line)
        if name and m:
            if entry is None:
                entry = int(m.group(1), 16)
            body.append((bytes.fromhex(m.group(2).replace(' ', '')), m.group(3)))
    return functions


def mask_for(text):
    """Which bits of this instruction does the linker rewrite?"""
    if '@ha' in text or '@l' in text:
        return 0xFFFF0000          # only the opcode/registers are stable
    if re.match(r'^(bl|b|ba|bla)\s+\w', text) and 'r' not in text.split()[1][:1]:
        return 0xFC000000          # branch displacement is relocated
    return 0xFFFFFFFF


def find(mem, body, entry):
    """Every address in `mem` where this function's stable bytes match."""
    words = [(struct.unpack('>I', b)[0], mask_for(t)) for b, t in body]
    # Anchor on the longest run of fully stable instructions, so the scan is
    # a plain substring search and only a handful of candidates are verified.
    best_i = best_n = 0
    i = 0
    while i < len(words):
        if words[i][1] == 0xFFFFFFFF:
            j = i
            while j < len(words) and words[j][1] == 0xFFFFFFFF:
                j += 1
            if j - i > best_n:
                best_i, best_n = i, j - i
            i = j
        else:
            i += 1
    if not best_n:
        return []
    anchor = b''.join(struct.pack('>I', words[i][0])
                      for i in range(best_i, best_i + best_n))
    out, at = [], mem.find(anchor)
    while at >= 0:
        start = at - best_i * 4
        if start >= 0 and start + len(words) * 4 <= len(mem):
            ok = all((struct.unpack_from('>I', mem, start + k * 4)[0] & m)
                     == (v & m) for k, (v, m) in enumerate(words))
            if ok:
                out.append(MEM1 + start)
        at = mem.find(anchor, at + 1)
    return out


def globals_in(functions, label):
    """(function, index of the lis, index of the @l use) for `label`."""
    for name, (entry, body) in functions.items():
        hi = None
        for k, (_, text) in enumerate(body):
            if label + '@ha' in text:
                hi = k
            elif hi is not None and label + '@l' in text:
                yield name, entry, body, hi, k
                hi = None


def main():
    if len(sys.argv) < 4:
        print(__doc__)
        print('usage: dolphin-locate.py <snapshot> <text.s> '
              '<fn_… | lbl_…> [...]')
        return 2
    mem = open(sys.argv[1], 'rb').read()
    functions = parse(sys.argv[2])
    print('%d functions in %s' % (len(functions), sys.argv[2]))
    for want in sys.argv[3:]:
        if want in functions:
            entry, body = functions[want]
            hits = find(mem, body, entry)
            print('%-22s %s' % (want, ' '.join('0x%08X' % h for h in hits)
                                or 'NOT RESIDENT'))
            for h in hits:
                print('%-22s   module offset 0x%06X -> text base 0x%08X'
                      % ('', entry, h - entry))
            continue
        found = False
        for name, entry, body, hi, lo in globals_in(functions, want):
            hits = find(mem, body, entry)
            if not hits:
                continue
            at = hits[0]
            a = struct.unpack_from('>I', mem, at - MEM1 + hi * 4)[0] & 0xFFFF
            b = struct.unpack_from('>I', mem, at - MEM1 + lo * 4)[0] & 0xFFFF
            addr = ((a << 16) + (b - 0x10000 if b & 0x8000 else b)) & 0xFFFFFFFF
            print('%-22s 0x%08X   (via %s at 0x%08X)' % (want, addr, name, at))
            found = True
            break
        if not found:
            print('%-22s could not be located' % want)
    return 0


if __name__ == '__main__':
    sys.exit(main())
