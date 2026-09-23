#!/usr/bin/env python3
"""Disassemble a span of a DOL or REL, to read one function.

WHY THIS EXISTS. Targeted reading of individual functions is in scope and is
often the fastest route through a stalled investigation - but there is no
PowerPC disassembler on this machine. The system objdump is built for x86
only (`objdump -i` lists i386 and bpf), the llvm-objdump on PATH rejects
`-b binary`, and the Ghidra here is a flatpak with no headless launcher.

So this decodes the subset that answers "why did this function take that
branch": loads and stores, the arithmetic that forms addresses, compares,
conditional branches, and calls. Anything it does not know is printed as its
opcode numbers rather than guessed at - a wrong mnemonic is worse than none,
because it would be believed.

ITS OUTPUT IS FOR HUMANS. Per the project's standing rules it is never
compiled in, never committed, and not quoted in notes: record what a
function DOES, not its instructions.

usage: ppc-dis.py <main.dol> <start-addr> [end-addr]
"""
import struct, sys

def dol_segments(d):
    h = struct.unpack('>64I', d[:256])
    segs = []
    for o, a, l in zip(h[0:7], h[18:25], h[36:43]):
        if l: segs.append((o, a, l))
    for o, a, l in zip(h[7:18], h[25:36], h[43:54]):
        if l: segs.append((o, a, l))
    return segs

def to_off(segs, addr):
    for o, a, l in segs:
        if a <= addr < a + l: return o + (addr - a)
    return None

def s16(v): return v - 0x10000 if v & 0x8000 else v

CMP = {0: 'cmpw', 32: 'cmplw'}
# D-form: opcode -> mnemonic. rA==0 means literal 0, not r0.
DFORM = {32: 'lwz', 33: 'lwzu', 34: 'lbz', 35: 'lbzu', 36: 'stw', 37: 'stwu',
         38: 'stb', 39: 'stbu', 40: 'lhz', 41: 'lhzu', 42: 'lha', 43: 'lhau',
         44: 'sth', 45: 'sthu', 48: 'lfs', 50: 'lfd', 52: 'stfs', 54: 'stfd'}

def decode(w, pc):
    op = w >> 26
    rD = (w >> 21) & 31; rA = (w >> 16) & 31; rB = (w >> 11) & 31
    d = s16(w & 0xFFFF)
    if w == 0x4E800020: return 'blr'
    if w == 0x4E800021: return 'blrl'
    if op == 18:                                    # b / bl / ba / bla
        li = w & 0x03FFFFFC
        if li & 0x02000000: li -= 0x04000000
        tgt = li if (w & 2) else pc + li
        return f"b{'l' if w & 1 else ''}      0x{tgt & 0xFFFFFFFF:08X}"
    if op == 16:                                    # bc
        bo = rD; bi = rA
        cr = bi >> 2; cond = ['lt', 'gt', 'eq', 'so'][bi & 3]
        taken = 'b' + (cond if bo & 8 else 'n' + cond)
        bd = w & 0xFFFC
        if bd & 0x8000: bd -= 0x10000
        tgt = bd if (w & 2) else pc + bd
        crs = '' if cr == 0 else f'cr{cr},'
        return f"{taken:<7}{crs}0x{tgt & 0xFFFFFFFF:08X}"
    if op == 11: return f"cmpwi   {'cr%d,' % (rD >> 2) if rD >> 2 else ''}r{rA},{d}"
    if op == 10: return f"cmplwi  {'cr%d,' % (rD >> 2) if rD >> 2 else ''}r{rA},{w & 0xFFFF}"
    if op == 31:
        xo = (w >> 1) & 0x3FF
        if xo in CMP: return f"{CMP[xo]:<7} {'cr%d,' % (rD >> 2) if rD >> 2 else ''}r{rA},r{rB}"
        if xo == 444: return (f"mr      r{rA},r{rD}" if rD == rB
                              else f"or      r{rA},r{rD},r{rB}")
        if xo == 266: return f"add     r{rD},r{rA},r{rB}"
        if xo == 40:  return f"subf    r{rD},r{rA},r{rB}"
        if xo == 339: return f"mfspr   r{rD},{((w >> 16) & 31) | ((w >> 6) & 0x3E0)}"
        if xo == 467: return f"mtspr   {((w >> 16) & 31) | ((w >> 6) & 0x3E0)},r{rD}"
        if xo == 23:  return f"lwzx    r{rD},r{rA},r{rB}"
        if xo == 151: return f"stwx    r{rD},r{rA},r{rB}"
        return f".long   0x{w:08X}   ; op31 xo={xo}"
    if op == 14: return (f"li      r{rD},{d}" if rA == 0
                         else f"addi    r{rD},r{rA},{d}")
    if op == 15: return (f"lis     r{rD},0x{w & 0xFFFF:04X}" if rA == 0
                         else f"addis   r{rD},r{rA},0x{w & 0xFFFF:04X}")
    if op == 21:
        sh = (w >> 11) & 31; mb = (w >> 6) & 31; me = (w >> 1) & 31
        return f"rlwinm  r{rA},r{rD},{sh},{mb},{me}"
    if op in DFORM:
        return f"{DFORM[op]:<7} r{rD},{d}(r{rA})"
    return f".long   0x{w:08X}   ; op={op}"

def main():
    if len(sys.argv) < 3:
        print(__doc__); return 2
    d = open(sys.argv[1], 'rb').read()
    segs = dol_segments(d)
    start = int(sys.argv[2], 0)
    end = int(sys.argv[3], 0) if len(sys.argv) > 3 else start + 0x100
    off = to_off(segs, start)
    if off is None:
        print(f"0x{start:08X} is in no segment"); return 1
    for pc in range(start, end, 4):
        o = to_off(segs, pc)
        w = struct.unpack('>I', d[o:o + 4])[0]
        print(f"  0x{pc:08X}  {w:08X}  {decode(w, pc)}")
    return 0

if __name__ == '__main__':
    sys.exit(main())
