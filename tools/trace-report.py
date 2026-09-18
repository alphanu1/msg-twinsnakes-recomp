#!/usr/bin/env python3
"""Resolve a dispatch trace against the symbol map.

game/module/dispatch.c writes an address histogram when MGS_DISPATCH_TRACE is
set. This says which functions the game is actually spinning in - the point of
having a symbol map at all.
"""
import re, sys, bisect, argparse

def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--trace', required=True)
    ap.add_argument('--symbols', default='config/symbols/main.dol.symbols.txt')
    ap.add_argument('--top', type=int, default=20)
    a = ap.parse_args()

    named = {}
    for line in open(a.symbols):
        m = re.match(r'^\S+ 0x([0-9A-Fa-f]+) 0x([0-9A-Fa-f]+|\?) (\S+)', line)
        if m:
            named[int(m.group(1), 16)] = m.group(3)
    addrs = sorted(named)

    rows, header = [], ''
    for line in open(a.trace):
        if line.startswith('#'):
            header = line.strip('# \n'); continue
        addr, hits = line.split()
        rows.append((int(addr, 16), int(hits)))

    print(header)
    print()
    rows.sort(key=lambda r: -r[1])
    total = sum(h for _, h in rows) or 1
    print(f"{'address':<12}{'hits':>12}{'share':>8}  where")
    for addr, hits in rows[:a.top]:
        if addr >= 0x80500000:
            where = f"REL +0x{addr - 0x805000EC:X}"
        else:
            i = bisect.bisect_right(addrs, addr) - 1
            where = named[addrs[i]] if i >= 0 else '?'
            if i >= 0 and addr != addrs[i]:
                where += f" +0x{addr - addrs[i]:X}"
        print(f"0x{addr:08X}{hits:>12,}{100*hits/total:>7.1f}%  {where}")

if __name__ == '__main__':
    sys.exit(main())
