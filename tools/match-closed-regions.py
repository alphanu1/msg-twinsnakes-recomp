#!/usr/bin/env python3
"""Name the unknowns in a region that is closed at both ends and exactly full.

The strongest evidence in this map has repeatedly been the same shape: two
named functions with N unknowns between them, and a reference file with
exactly N functions between those two names, in that order. Nothing is
inlined, nothing is stripped, nothing is a guess about where a file begins -
the region is closed at both ends and there is precisely one way to fill it.

F159's warning stands: a matching count alone is necessary, not sufficient.
What makes this different is that the count is taken inside a region pinned
by two known names from the same translation unit, and any call evidence the
binary does carry must agree as well. Where a function's callees contradict
the slot it would take, the whole region is refused rather than patched.
"""
import re, glob, os, argparse, collections

def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--symbols', required=True)
    ap.add_argument('--boundaries', required=True)
    ap.add_argument('--asm', required=True)
    ap.add_argument('--reference', required=True, nargs='+')
    ap.add_argument('--max-addr', default='0x8004A000')
    a = ap.parse_args()

    named = {}
    for ln in open(a.symbols):
        f = ln.split()
        if len(f) >= 4 and f[1].startswith('0x'):
            try: named[int(f[1], 16)] = f[3]
            except ValueError: pass
    addrs = set(named)
    for ln in open(a.boundaries):
        m = re.match(r'fn_([0-9A-Fa-f]{8}) = ', ln)
        if m: addrs.add(int(m.group(1), 16))
    addrs = sorted(addrs)

    cur, calls = None, collections.defaultdict(set)
    for p in sorted(glob.glob(a.asm)):
        for ln in open(p, errors='ignore'):
            m = re.match(r'\.fn (\w+)', ln)
            if m: cur = m.group(1); continue
            m = re.search(r'\bbl\s+(\w+)', ln)
            if m and cur and cur.startswith('fn_'):
                t = m.group(1)
                mm = re.match(r'fn_([0-9A-Fa-f]{8})$', t)
                if mm: t = named.get(int(mm.group(1), 16), t)
                if not t.startswith('fn_'): calls[int(cur[3:], 16)].add(t)

    # reference: per file, the ordered function list and each one's callees
    files = {}
    for root in a.reference:
        for p in sorted(glob.glob(os.path.join(root, '**', '*.c'), recursive=True)):
            txt = re.sub(r'/\*.*?\*/', '', open(p, errors='ignore').read(), flags=re.S)
            seq = []
            for m in re.finditer(r'^[A-Za-z_][\w \t\*]*?\b(\w+)\s*\([^;{]*\)\s*\{', txt, re.M):
                nm, st, d, body = m.group(1), m.end() - 1, 0, ''
                for j in range(st, len(txt)):
                    if txt[j] == '{': d += 1
                    elif txt[j] == '}':
                        d -= 1
                        if d == 0: body = txt[st:j]; break
                seq.append((nm, set(re.findall(r'\b([A-Za-z_]\w*)\s*\(', body))))
            key = f'{os.path.basename(root)}/{os.path.basename(p)}'
            if seq: files[key] = seq
    index = {}
    for k, seq in files.items():
        for i, (nm, _) in enumerate(seq): index.setdefault((k, nm), i)

    limit = int(a.max_addr, 16)
    proposed, refused = [], 0
    named_addrs = [x for x in addrs if x in named and x < limit]
    for p1, p2 in zip(named_addrs, named_addrs[1:]):
        gap = [x for x in addrs if p1 < x < p2]
        if not gap: continue
        for key, seq in files.items():
            i1, i2 = index.get((key, named[p1])), index.get((key, named[p2]))
            if i1 is None or i2 is None or i2 <= i1: continue
            between = seq[i1 + 1:i2]
            if len(between) != len(gap): continue
            # every call the binary makes must be one the slot's function makes
            ok = all(calls.get(x, set()) <= c for x, (_, c) in zip(gap, between))
            if ok: proposed.append((gap, [n for n, _ in between], key))
            else:
                refused += 1
                # A REFUSAL IS WORTH READING, NOT JUST COUNTING.
                # The region is the right size and pinned at both ends, yet a
                # call disagrees - which is either a version difference or a
                # name already in the map being wrong. The latter has happened
                # before, so print it rather than swallow it.
                for x, (nm, c) in zip(gap, between):
                    bad = calls.get(x, set()) - c
                    if bad:
                        print(f'  REFUSED {key}: 0x{x:08X} would be {nm}, '
                              f'but it calls {", ".join(sorted(bad))}')
            break

    seen = collections.Counter(n for _, ns, _ in proposed for n in ns)
    clean = [(g, ns, k) for g, ns, k in proposed if all(seen[n] == 1 for n in ns)]
    total = sum(len(g) for g, _, _ in clean)
    print(f'{len(clean)} closed regions exactly filled ({total} functions); '
          f'{refused} refused because a call contradicted its slot; '
          f'{len(proposed) - len(clean)} dropped for proposing a name twice')
    for gap, names, key in clean:
        print(f'  {key}')
        for addr, nm in zip(gap, names):
            ev = ", ".join(sorted(calls.get(addr, set())))[:44]
            print(f'    0x{addr:08X}  {nm:<26} {ev}')

main()
