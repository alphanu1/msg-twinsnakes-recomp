#!/usr/bin/env python3
"""Name SDK functions from the company they keep.

Two facts make this work, and both are measured rather than assumed:

  * Within a translation unit the linker emitted functions in SOURCE ORDER.
    Checked against the CARD module, where 24 names are already confirmed by
    other routes: 15 consecutive pairs ascend and none descend.
  * A function's set of callees survives compilation. Names we already have
    turn a binary call into a fact that can be compared with the reference.

So an unnamed function bounded by two named ones can only be something that
lies between them in the source, and its callees usually pick exactly one.
Where they pick more than one - three accessors that differ only in which
field they return - nothing is proposed. That is the F159 rule: a match that
does not exclude the alternatives is not a match.

The reference is the doldecomp community decompilation in extern/dolsdk2004,
read for names and call structure only. Nothing is copied from it.
"""
import re, os, glob, argparse, collections

def dol_functions(sym_path, boundaries):
    named = {}
    for ln in open(sym_path):
        f = ln.split()
        if len(f) >= 4 and f[1].startswith('0x'):
            try: named[int(f[1], 16)] = f[3]
            except ValueError: pass
    addrs = set(named)
    for ln in open(boundaries):
        m = re.match(r'fn_([0-9A-Fa-f]{8}) = ', ln)
        if m: addrs.add(int(m.group(1), 16))
    return named, sorted(addrs)

def binary_callees(asm_glob, named):
    cur, out = None, collections.defaultdict(list)
    for p in sorted(glob.glob(asm_glob)):
        for ln in open(p, errors='ignore'):
            m = re.match(r'\.fn (\w+)', ln)
            if m: cur = m.group(1); continue
            m = re.search(r'\bbl\s+(\w+)', ln)
            if m and cur:
                t = m.group(1)
                mm = re.match(r'fn_([0-9A-Fa-f]{8})$', t)
                if mm: t = named.get(int(mm.group(1), 16), t)
                out[cur].append(t)
    return out

def ref_callers(src_dir, names):
    """Which reference functions mention each name. The caller side is
    evidence the callee side cannot see, so agreeing with it is a second
    route and not a restatement of the first."""
    out = collections.defaultdict(set)
    pat = {n: re.compile(r'\b' + re.escape(n) + r'\b') for n in names}
    for p in glob.glob(os.path.join(src_dir, '**', '*.c'), recursive=True):
        txt = re.sub(r'/\*.*?\*/', '', open(p, errors='ignore').read(), flags=re.S)
        for m in re.finditer(r'^[A-Za-z_][\w \t\*]*?\b(\w+)\s*\([^;{]*\)\s*\{', txt, re.M):
            fn, st, d = m.group(1), m.end() - 1, 0
            body = ''
            for j in range(st, len(txt)):
                if txt[j] == '{': d += 1
                elif txt[j] == '}':
                    d -= 1
                    if d == 0: body = txt[st:j]; break
            for n, rx in pat.items():
                if fn != n and rx.search(body): out[n].add(fn)
    return out


def binary_callers(asm_glob, named, addrs):
    """Which named functions mention each address."""
    cur, out = None, collections.defaultdict(set)
    want = set(addrs)
    for p in sorted(glob.glob(asm_glob)):
        for ln in open(p, errors='ignore'):
            m = re.match(r'\.fn (\w+)', ln)
            if m: cur = m.group(1); continue
            for t in re.findall(r'\bfn_([0-9A-Fa-f]{8})\b', ln):
                a = int(t, 16)
                if a in want and cur:
                    c = named.get(int(cur[3:], 16), cur) if cur.startswith('fn_') else cur
                    if not c.startswith('fn_'): out[a].add(c)
    return out


def reference(src_dir):
    """Function names in source order per file, with each one's callees."""
    order, calls = {}, {}
    for p in sorted(glob.glob(os.path.join(src_dir, '**', '*.c'), recursive=True)):
        txt = re.sub(r'/\*.*?\*/', '', open(p, errors='ignore').read(), flags=re.S)
        base = os.path.basename(p)
        idx = 0
        for m in re.finditer(r'^[A-Za-z_][\w \t\*]*?\b(\w+)\s*\([^;{]*\)\s*\{', txt, re.M):
            name, start, d = m.group(1), m.end() - 1, 0
            for j in range(start, len(txt)):
                if txt[j] == '{': d += 1
                elif txt[j] == '}':
                    d -= 1
                    if d == 0:
                        body = txt[start:j]; break
            else:
                body = ''
            order.setdefault(name, (base, idx))
            calls[name] = set(re.findall(r'\b([A-Za-z_]\w*)\s*\(', body))
            idx += 1
    return order, calls

def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--symbols', required=True)
    ap.add_argument('--boundaries', required=True)
    ap.add_argument('--asm', required=True)
    ap.add_argument('--reference', required=True)
    ap.add_argument('--max-addr', default='0x8004A000')
    a = ap.parse_args()

    named, addrs = dol_functions(a.symbols, a.boundaries)
    callees = binary_callees(a.asm, named)
    order, ref_calls = reference(a.reference)
    limit = int(a.max_addr, 16)

    proposed, ambiguous, unbounded = [], 0, 0
    for i, addr in enumerate(addrs):
        if addr in named or addr >= limit: continue
        prev = next((addrs[j] for j in range(i - 1, -1, -1) if addrs[j] in named), None)
        nxt  = next((addrs[j] for j in range(i + 1, len(addrs)) if addrs[j] in named), None)
        if prev is None or nxt is None: unbounded += 1; continue
        lo, hi = order.get(named[prev]), order.get(named[nxt])
        if not lo or not hi or (lo[0] == hi[0] and hi[1] - lo[1] < 2):
            unbounded += 1; continue

        # BOUNDED ON ONE SIDE IS STILL BOUNDED.
        #
        # Requiring both neighbours to land in the same reference file left
        # 322 functions untouched, because a run of unnamed code often
        # straddles a file boundary. What a neighbour actually establishes is
        # a floor or a ceiling: anything after a named function is later in
        # ITS file, anything before one is earlier in THAT file. Taking the
        # union widens the candidate set, and the callee and caller tests do
        # the discriminating - which is where the burden belongs.
        if lo[0] == hi[0]:
            cands = [n for n, (f, k) in order.items() if f == lo[0] and lo[1] < k < hi[1]]
        else:
            cands = [n for n, (f, k) in order.items()
                     if (f == lo[0] and k > lo[1]) or (f == hi[0] and k < hi[1])]
        mine = set(callees.get('fn_%08X' % addr, []))
        known = {c for c in mine if not c.startswith('fn_')}
        if not known: ambiguous += 1; continue

        # TWO CALLS EVERY SDK FUNCTION MAKES ARE NOT EVIDENCE.
        #
        # Bracketing a critical section with OSDisableInterrupts and
        # OSRestoreInterrupts is close to universal in this SDK, so a match
        # rooted only in those says nothing about which function this is - it
        # says the author was careful. A proposal must rest on at least one
        # callee that is not part of that furniture.
        GENERIC = {'OSDisableInterrupts', 'OSRestoreInterrupts', 'OSReport',
                   'memcpy', 'memset', '__assert', 'OSPanic'}
        if not (known - GENERIC):
            ambiguous += 1; continue

        hits = [c for c in cands if known <= ref_calls.get(c, set())]
        if len(hits) == 1:
            proposed.append((addr, hits[0], lo[0], sorted(known)))
        else:
            ambiguous += 1

    # A NAME PROPOSED TWICE IS A NAME PROVED NOWHERE.
    #
    # Static helpers share names across translation units - three different
    # functions came back as "Retry" - and the reference index collapses
    # them, so a repeated proposal means the evidence did not distinguish the
    # files either. Drop every one of them rather than pick.
    counts = collections.Counter(n for _, n, _, _ in proposed)
    dropped = [p for p in proposed if counts[p[1]] > 1]
    proposed = [p for p in proposed if counts[p[1]] == 1]
    if dropped:
        print(f'dropped {len(dropped)} proposals whose name was proposed more '
              f'than once: {sorted({n for _, n, _, _ in dropped})}')

    # THE SECOND ROUTE, APPLIED HERE RATHER THAN BY HAND.
    bc = binary_callers(a.asm, named, [p[0] for p in proposed])
    rc = ref_callers(a.reference, {p[1] for p in proposed})
    confirmed, single = [], []
    for addr, name, f, k in proposed:
        agree = bc.get(addr, set()) & rc.get(name, set())
        (confirmed if agree else single).append((addr, name, f, k, sorted(agree)))
    if single:
        print(f'{len(single)} passed the callee test but have no agreeing '
              f'caller, so are NOT claimed:')
        for addr, name, f, k, _ in single:
            print(f'  0x{addr:08X}  {name:<28} {f}')
    proposed = confirmed

    print(f'proposed {len(proposed)}; {ambiguous} had no unique match; '
          f'{unbounded} were not bounded by two named neighbours in one file')
    for addr, name, f, k, agree in proposed:
        print(f'  0x{addr:08X}  {name:<28} {f:<14} calls {", ".join(k)[:34]}'
              f'  <- called by {", ".join(agree)[:30]}')

main()
