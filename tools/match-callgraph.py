#!/usr/bin/env python3
"""Name SDK functions by the set of already-named functions they call.

Stage 5d of docs/decompilation-process.md.

Ordered run alignment (stage 5) is exhausted: three independent reference
decomps now agree on 567 names and add nothing further. What they cannot do is
name a function the reference games never linked, and 185 of the SDK entry
points this game's engine calls are in exactly that position.

A function's CALLEES are a fingerprint, and a strong one. `__OSInitAudioSystem`
is the SDK function that calls `DSPInit`, `__OSLockSram` and `OSRegisterVersion`
in that combination; nothing else does. So for each unnamed function we take
the multiset of NAMED functions it calls, and look for the decompiled SDK
function whose own callees match. A unique match, with enough calls to be
distinctive, is a name.

WHY THIS IS SOUND, AND WHERE IT IS NOT:

  - The evidence is our own binary's call graph, recovered from our own
    disassembly. The decomp supplies only a name to put on a shape we
    established independently.
  - A match on one or two common callees is worthless - half the SDK calls
    OSDisableInterrupts. So a minimum distinctiveness is required, and callees
    that appear in very many candidates count for less.
  - Ambiguity is refused rather than resolved by preference. Two candidates
    matching equally well means the evidence does not distinguish them, and a
    guess here would be indistinguishable from a fact in config/symbols/.

No code is copied. The output is names, each recorded with the origin
`callgraph` so it can be re-derived or withdrawn per symbol.
"""
import re, sys, argparse, collections, os

BL = re.compile(r'^\s*bl\s+(?:fn_)?(\w+)', re.M)

def our_callgraph(asm_paths, named):
    """address -> Counter of named callee names, from our own disassembly."""
    graph = collections.defaultdict(collections.Counter)
    cur = None
    # dtk writes `.fn fn_800055E0, global` at each function's start, and every
    # instruction line carries its address in a comment. Anchoring on the .fn
    # directive is what keeps a call attributed to the function it is in -
    # local labels look like function starts and are not.
    fn_start = re.compile(r'^\s*\.fn\s+(?:fn_)?([0-9A-Za-z_]*?)(8[0-9A-Fa-f]{7})\b')
    bl_line = re.compile(r'\bbl\s+(\S+)\s*$')
    for path in asm_paths:
        for line in open(path, errors='replace'):
            m = fn_start.match(line)
            if m:
                cur = int(m.group(2), 16)
                continue
            m = bl_line.search(line.rstrip())
            if m and cur is not None:
                t = m.group(1).strip()
                tm = re.match(r'^(?:fn_)?(8[0-9A-Fa-f]{7})$', t)
                if tm:
                    a = int(tm.group(1), 16)
                    if a in named:
                        graph[cur][named[a]] += 1
                elif re.match(r'^[A-Za-z_]', t):
                    graph[cur][t] += 1
    return graph

# A C function definition at column 0, then its body to the closing brace.
C_DEF = re.compile(r'^(?:static\s+)?(?:asm\s+)?[A-Za-z_][\w\s\*]*?\b([A-Za-z_]\w*)\s*\([^;]*?\)\s*\{', re.M)
CALL  = re.compile(r'\b([A-Za-z_]\w*)\s*\(')

KEYWORDS = {'if','for','while','switch','return','sizeof','do','else','case'}

def sdk_callgraph(src_roots):
    """name -> Counter of callee names, from decompiled/original source."""
    graph = {}
    for src_root in src_roots:
      for dirpath, _, files in os.walk(src_root):
        for f in files:
            if not f.endswith(('.c', '.cpp')):
                continue
            path = os.path.join(dirpath, f)
            try:
                text = open(path, errors='replace').read()
            except OSError:
                continue
            for m in C_DEF.finditer(text):
                name = m.group(1)
                if name in KEYWORDS:
                    continue
                # Body: from the opening brace to its match.
                i = text.index('{', m.end() - 1)
                depth, j = 0, i
                while j < len(text):
                    if text[j] == '{': depth += 1
                    elif text[j] == '}':
                        depth -= 1
                        if depth == 0: break
                    j += 1
                body = text[i:j]
                calls = collections.Counter(
                    c for c in CALL.findall(body) if c not in KEYWORDS and c != name)
                # Inline assembly calls a function by `bl name`.
                for t in BL.findall(body):
                    calls[t] += 1
                if calls:
                    graph[name] = calls
    return graph

def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--asm', nargs='+', required=True,
                    help='our own disassembly of the DOL')
    ap.add_argument('--symbols', required=True, help='config/symbols map')
    ap.add_argument('--boundaries', required=True,
                    help='dtk symbol map with EVERY function and its size. '
                         'The names map only covers named functions, so sizes '
                         'for new names have to come from the boundary map.')
    ap.add_argument('--sdk-src', required=True, nargs='+',
                    help='source trees to take names from')
    ap.add_argument('--file-map',
                    help='output of attribute-by-strings.py: address -> source '
                         'file. Where a function\'s file is known, candidates '
                         'are restricted to that file, which turns most '
                         'ambiguity into a unique match.')
    ap.add_argument('--targets', help='file of addresses to prioritise, one per line')
    ap.add_argument('--min-distinct', type=int, default=3,
                    help='named callees a match must share (default 3)')
    ap.add_argument('--rounds', type=int, default=6,
                    help='iterate until nothing new is confirmed (default 6)')
    ap.add_argument('--out')
    a = ap.parse_args()

    named = {}
    for line in open(a.symbols):
        m = re.match(r'^(\S+) 0x([0-9A-Fa-f]+) 0x([0-9A-Fa-f]+) (\S+)', line)
        if m:
            named[int(m.group(2), 16)] = m.group(4)

    sizes = {}
    for line in open(a.boundaries):
        m = re.match(r'^\S+ = (\.\w+):0x([0-9A-Fa-f]+); // type:function size:0x([0-9A-Fa-f]+)', line)
        if m:
            sizes[int(m.group(2), 16)] = int(m.group(3), 16)

    # Which source file each function came from, if a string told us.
    file_of = {}
    if a.file_map:
        for line in open(a.file_map):
            m = re.match(r'^0x([0-9A-Fa-f]+)\s+(\S+)', line)
            if m: file_of[int(m.group(1), 16)] = m.group(2)

    ours = our_callgraph(a.asm, named)
    sdk = sdk_callgraph(a.sdk_src)
    print(f"our call graph   {len(ours):>6} functions with calls")
    print(f"SDK call graph   {len(sdk):>6} functions with calls")

    # How common is each callee across the SDK? A callee that appears
    # everywhere carries almost no information; one that appears twice
    # carries a great deal.
    freq = collections.Counter()
    for calls in sdk.values():
        for c in calls:
            freq[c] += 1

    # Where each decompiled function came from, for the independent check.
    where, defined_in = {}, {}
    for root in a.sdk_src:
        for dirpath, _, files in os.walk(root):
            for f in files:
                if not f.endswith(('.c', '.cpp')):
                    continue
                path = os.path.join(dirpath, f)
                module = os.path.basename(os.path.dirname(path))
                try:
                    text = open(path, errors='replace').read()
                except OSError:
                    continue
                for m in C_DEF.finditer(text):
                    where.setdefault(m.group(1), module)
                    defined_in.setdefault(m.group(1), f)

    # ITERATE TO A FIXPOINT. Every name confirmed this round is a named
    # callee next round, so functions that were below the distinctiveness
    # threshold can cross it. That is not a trick to get more names: a
    # function whose callees are all unnamed is genuinely unidentifiable by
    # this method, and naming its callees genuinely identifies it. The rounds
    # stop when nothing new is confirmed.
    confirmed_total = {}
    for round_no in range(1, a.rounds + 1):
        found = one_round(ours, sdk, named, freq, where, defined_in,
                          file_of, a, round_no)
        if not found:
            break
        for addr, name in found.items():
            named[addr] = name
            confirmed_total[addr] = name

    print()
    print(f"CONFIRMED in total {len(confirmed_total):>6}")

    if a.out:
        with open(a.out, 'w') as f:
            for addr in sorted(confirmed_total):
                f.write(f".text 0x{addr:08X} 0x{sizes.get(addr, 0):X} "
                        f"{confirmed_total[addr]} callgraph\n")
        print(f"wrote {a.out}")

def one_round(ours, sdk, named, freq, where, defined_in, file_of, a, round_no):
    taken = set(named.values())
    assigned, ambiguous, weak = {}, 0, 0

    for addr, calls in ours.items():
        if addr in named:
            continue
        mine = set(calls)
        need = a.min_distinct
        if addr in file_of:
            need = min(need, 2)      # the file is already strong evidence
        if len(mine) < need:
            weak += 1
            continue

        # If a string in the binary says which file this function came from,
        # only that file's functions are candidates. This is the strongest
        # constraint available and it costs nothing: it is not a heuristic
        # about what the function resembles, it is where it was compiled from.
        want_file = file_of.get(addr)

        scored = []
        for name, theirs in sdk.items():
            if name in taken:
                continue
            if want_file and defined_in.get(name) != want_file:
                continue
            shared = mine & set(theirs)
            if len(shared) < need:
                continue
            # Weight by rarity: 1/frequency. A shared call to something used
            # by 200 functions is nearly free; one used by 2 is decisive.
            score = sum(1.0 / freq[c] for c in shared)
            # Penalise a candidate that calls a great deal we do not.
            extra = len(set(theirs) - mine)
            scored.append((score, -extra, name, shared))

        if not scored:
            continue
        scored.sort(reverse=True)
        best = scored[0]
        if len(scored) > 1 and scored[1][0] > best[0] * 0.75:
            ambiguous += 1          # the evidence does not distinguish them
            continue
        assigned[addr] = (best[2], sorted(best[3]), best[0])

    # One name, one address. Two addresses claiming the same function is a
    # contradiction, not a near miss: at most one can be right, and nothing
    # here says which. Both are withdrawn - a wrong name in config/symbols/ is
    # worse than a missing one, because it is indistinguishable from a fact.
    by_name = collections.defaultdict(list)
    for addr, (name, shared, score) in assigned.items():
        by_name[name].append(addr)
    collisions = 0
    for name, addrs in by_name.items():
        if len(addrs) > 1:
            collisions += len(addrs)
            for a_ in addrs:
                del assigned[a_]

    print()
    print(f"--- round {round_no}")
    print(f"named            {len(assigned):>6}")
    print(f"collisions       {collisions:>6} (one name claimed by two addresses; all withdrawn)")
    print(f"ambiguous        {ambiguous:>6} (two candidates fit; refused)")
    print(f"too few calls    {weak:>6} (below --min-distinct)")

    # ---- the independent check ------------------------------------------
    #
    # A call-graph match is evidence from one direction only. The linker gives
    # a second, entirely unrelated one: functions from the same source file
    # are laid down TOGETHER, so a correctly named function should sit among
    # neighbours from its own SDK module. `CARDInit` surrounded by EXI and DVD
    # functions would be a match on shape that is wrong in fact.
    #
    # This is the same argument stage 5's run alignment rests on, applied to a
    # different result, and it costs nothing - the source file each decompiled
    # function came from is already known.
    where, defined_in = {}, {}
    for root in a.sdk_src:
        for dirpath, _, files in os.walk(root):
            for f in files:
                if not f.endswith(('.c', '.cpp')):
                    continue
                path = os.path.join(dirpath, f)
                module = os.path.basename(os.path.dirname(path))
                try:
                    text = open(path, errors='replace').read()
                except OSError:
                    continue
                for m in C_DEF.finditer(text):
                    where.setdefault(m.group(1), module)
                    defined_in.setdefault(m.group(1), f)

    # Neighbours are only evidence if they are NEAR. The SDK's modules are
    # laid down in blocks, so a function 200 KB away says nothing about this
    # one - and treating it as evidence rejected eight correct CARD names
    # because the nearest already-named symbol happened to be in `ar`.
    NEAR = 0x4000

    # Newly assigned names count as neighbours too, and that is the point:
    # eight CARD functions found independently, sitting contiguously, are
    # eight pieces of evidence for each other. A lone name with no module
    # agreement nearby stays unconfirmed rather than being discarded - it is
    # unsupported, not contradicted.
    module_at = {}
    for addr, name in named.items():
        if name in where: module_at[addr] = where[name]
    for addr, (name, _s, _sc) in assigned.items():
        if name in where: module_at[addr] = where[name]

    agree = disagree = unknown = 0
    checked = {}
    for addr, (name, shared, score) in sorted(assigned.items()):
        # A function whose SOURCE FILE was read out of the binary has already
        # been checked by a second, independent route - the string reference
        # is evidence from the data section, not from the call graph. Asking
        # it to also sit among neighbours of the same module is a third
        # demand, and a wrong one for a third-party library: libvorbis's files
        # are one directory, so its module never matches the SDK modules
        # around it.
        if addr in file_of:
            agree += 1
            checked[addr] = (name, shared, 'ok (file string)')
            continue
        mine = where.get(name)
        if not mine:
            unknown += 1
            checked[addr] = (name, shared, '?')
            continue
        near = [m for a2, m in module_at.items()
                if a2 != addr and abs(a2 - addr) <= NEAR]
        if not near:
            unknown += 1
            checked[addr] = (name, shared, 'isolated')
        elif mine in near:
            agree += 1
            checked[addr] = (name, shared, 'ok')
        else:
            disagree += 1
            checked[addr] = (name, shared, 'MISMATCH: ' + mine +
                             ' among ' + ','.join(sorted(set(near))))

    print()
    print("independent check - does each name sit among its own SDK module?")
    print(f"  agrees         {agree:>6}")
    print(f"  DISAGREES      {disagree:>6} (withdrawn)")
    print(f"  isolated       {unknown:>6} (kept, unconfirmed)")
    print()

    for addr in sorted(checked):
        name, shared, verdict = checked[addr]
        if verdict.startswith('MISMATCH'):
            print(f"  0x{addr:08X}  {name:<30} [{verdict}]")

    # A name that fails the second route is not kept. The two routes are
    # independent, so disagreement means at least one is wrong.
    for addr, (_n, _s, verdict) in checked.items():
        if verdict.startswith('MISMATCH'):
            assigned.pop(addr, None)

    return {addr: v[0] for addr, v in assigned.items()}

if __name__ == '__main__':
    main()
