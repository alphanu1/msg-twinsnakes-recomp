#!/usr/bin/env python3
"""Merge newly recovered names into config/symbols, refusing anything unsafe.

Every naming stage produces candidates; this is the one place they enter the
map, so it is the one place the map's invariants are enforced. A wrong name in
`config/symbols/` is worse than a missing one - it is indistinguishable from a
fact, it propagates into the patch table and the generated code, and nothing
downstream will question it.

Refused, always:

  - an address that is already named, or a name already used elsewhere;
  - a size that disagrees with the function boundary dtk recovered, which
    means the candidate is not the function it claims to be;
  - an address that is not a function boundary at all;
  - two candidates claiming the same address with DIFFERENT names, or the
    same name at two addresses - both are withdrawn, because at most one can
    be right and nothing here says which.

Accepted specially: two stages proposing the SAME name for the same address.
That is independent corroboration, and it is recorded as such in the origin
(`callgraph+fileline`) so the strength of the evidence survives in the map.
"""
import re, sys, argparse, collections

def load_named(path):
    by_addr, by_name = {}, {}
    for line in open(path):
        m = re.match(r'^(\.\w+) 0x([0-9A-Fa-f]+) 0x([0-9A-Fa-f]+) (\S+) (\S+)', line)
        if m:
            by_addr[int(m.group(2), 16)] = m.group(4)
            by_name[m.group(4)] = int(m.group(2), 16)
    return by_addr, by_name

def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--symbols', required=True)
    ap.add_argument('--boundaries', required=True)
    ap.add_argument('--add', nargs='+', required=True)
    ap.add_argument('--note', help='a line recorded above the added block')
    ap.add_argument('--dry-run', action='store_true')
    a = ap.parse_args()

    text = open(a.symbols).read()
    by_addr, by_name = load_named(a.symbols)

    bounds = {}
    for line in open(a.boundaries):
        m = re.match(r'^\S+ = (\.\w+):0x([0-9A-Fa-f]+); // type:function size:0x([0-9A-Fa-f]+)', line)
        if m: bounds[int(m.group(2), 16)] = int(m.group(3), 16)

    new, rejected = {}, collections.Counter()
    for path in a.add:
        for line in open(path):
            m = re.match(r'^(\.\w+) 0x([0-9A-Fa-f]+) 0x([0-9A-Fa-f]+) (\S+) (\S+)', line)
            if not m: continue
            addr, size, name, origin = (int(m.group(2), 16), int(m.group(3), 16),
                                        m.group(4), m.group(5))
            if addr in by_addr:  rejected['already named'] += 1; continue
            if name in by_name:  rejected['name already used'] += 1; continue
            if addr not in bounds:
                rejected['not a function boundary'] += 1; continue
            if size != bounds[addr]:
                rejected['size disagrees with boundary'] += 1; continue
            if addr in new:
                if new[addr][0] == name:
                    # Two stages, one answer. Corroboration, not a clash.
                    new[addr] = (name, size, new[addr][2] + '+' + origin)
                else:
                    rejected['two names for one address'] += 2
                    del new[addr]
                continue
            new[addr] = (name, size, origin)

    counts = collections.Counter(v[0] for v in new.values())
    for addr in [k for k, v in list(new.items()) if counts[v[0]] > 1]:
        rejected['one name, two addresses'] += 1
        del new[addr]

    print(f"accepted {len(new)}")
    for why, n in rejected.most_common():
        print(f"  refused {n:>4}  {why}")
    for addr in sorted(new):
        if '+' in new[addr][2]:
            print(f"  corroborated by two stages: {new[addr][0]}")

    if a.dry_run or not new:
        return 0

    block = '\n'.join(f".text 0x{addr:08X} 0x{new[addr][1]:X} "
                      f"{new[addr][0]} {new[addr][2]}" for addr in sorted(new))
    note = f"\n# {a.note}\n" if a.note else "\n"
    open(a.symbols, 'w').write(text.rstrip('\n') + '\n' + note + block + '\n')
    print(f"merged into {a.symbols}")
    return 0

if __name__ == '__main__':
    sys.exit(main())
