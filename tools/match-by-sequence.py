#!/usr/bin/env python3
"""Name a RUN of consecutive unknown functions by aligning it to a source file.

Matching one function at a time asks a weak question: "could this be that?"
Several candidates usually can be, so most attempts end ambiguous. Aligning a
whole run asks a much stronger one: "is there exactly one way to lay these
functions against that file, in order, so that every call pattern agrees?"

It works because two things hold together (both measured, see stage 5j):
translation units are emitted in source order, and a function's callees
survive compilation. So a run of five unknowns between two named functions is
five constraints that must be satisfied at once, by one assignment.

Functions the compiler inlined or the linker dropped are allowed for: the
reference sequence may skip, the binary sequence may not. Where more than one
alignment satisfies everything, NOTHING is proposed - an alignment that does
not exclude the alternatives has not identified anything.
"""
import re, glob, os, argparse, collections

def load(symbols, boundaries):
    named = {}
    for ln in open(symbols):
        f = ln.split()
        if len(f) >= 4 and f[1].startswith('0x'):
            try: named[int(f[1], 16)] = f[3]
            except ValueError: pass
    addrs = set(named)
    for ln in open(boundaries):
        m = re.match(r'fn_([0-9A-Fa-f]{8}) = ', ln)
        if m: addrs.add(int(m.group(1), 16))
    return named, sorted(addrs)

def binary_calls(asm_glob, named):
    cur, out = None, collections.defaultdict(set)
    for p in sorted(glob.glob(asm_glob)):
        for ln in open(p, errors='ignore'):
            m = re.match(r'\.fn (\w+)', ln)
            if m: cur = m.group(1); continue
            m = re.search(r'\bbl\s+(\w+)', ln)
            if m and cur and cur.startswith('fn_'):
                t = m.group(1)
                mm = re.match(r'fn_([0-9A-Fa-f]{8})$', t)
                if mm: t = named.get(int(mm.group(1), 16), t)
                if not t.startswith('fn_'): out[int(cur[3:], 16)].add(t)
    return out

def reference(src_dir):
    files = {}
    for p in sorted(glob.glob(os.path.join(src_dir, '**', '*.c'), recursive=True)):
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
        if seq: files[os.path.basename(p)] = seq
    return files

def alignments(run, seq, vocabulary, cap=200):
    """Every order-preserving assignment of run -> seq. Counted, not just
    found: the point is to know whether the answer is unique."""
    found = []
    def walk(i, j, acc):
        if len(found) > cap: return
        if i == len(run):
            found.append(list(acc)); return
        if j == len(seq): return
        mine = run[i]
        name, calls = seq[j]
        if mine <= calls and (calls & vocabulary) <= mine:
            acc.append(name); walk(i + 1, j + 1, acc); acc.pop()
        walk(i, j + 1, acc)          # reference entry inlined or stripped
    walk(0, 0, [])
    return found

def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--symbols', required=True)
    ap.add_argument('--boundaries', required=True)
    ap.add_argument('--asm', required=True)
    ap.add_argument('--reference', required=True)
    ap.add_argument('--max-addr', default='0x8004A000')
    ap.add_argument('--min-run', type=int, default=2)
    ap.add_argument('--forward-only', action='store_true',
                    help='drop the reverse check, which inlining breaks')
    a = ap.parse_args()

    named, addrs = load(a.symbols, a.boundaries)
    calls = binary_calls(a.asm, named)
    files = reference(a.reference)
    where = {nm: (f, i) for f, seq in files.items() for i, (nm, _) in enumerate(seq)}
    vocabulary = set(named.values())
    limit = int(a.max_addr, 16)

    proposed, ambiguous, skipped = [], 0, 0
    i = 0
    while i < len(addrs):
        if addrs[i] in named or addrs[i] >= limit: i += 1; continue
        j = i
        while j < len(addrs) and addrs[j] not in named and addrs[j] < limit: j += 1
        run_addrs = addrs[i:j]
        prev = addrs[i - 1] if i else None
        nxt = addrs[j] if j < len(addrs) else None
        i = j
        if len(run_addrs) < a.min_run or prev is None or prev not in named: continue
        loc = where.get(named[prev])
        if not loc: continue
        fname, lo = loc
        seq = files[fname][lo + 1:]
        hi = where.get(named.get(nxt, ''), (None, None))
        if hi[0] == fname: seq = files[fname][lo + 1:hi[1]]
        if not seq: continue

        sig = [calls.get(x, set()) for x in run_addrs]
        al = alignments(sig, seq, set() if a.forward_only else vocabulary)
        if len(al) == 1:
            proposed.append((run_addrs, al[0], fname))
        elif al:
            ambiguous += 1
        else:
            skipped += 1

    named_count = sum(len(r) for r, _, _ in proposed)
    print(f'{len(proposed)} runs aligned uniquely ({named_count} functions); '
          f'{ambiguous} runs had more than one valid alignment; '
          f'{skipped} had none')
    for run, names, f in proposed:
        print(f'  {f}')
        for addr, nm in zip(run, names):
            print(f'    0x{addr:08X}  {nm}')

main()
