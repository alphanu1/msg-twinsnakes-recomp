#!/usr/bin/env python3
"""Name a function from the diagnostic message it prints about itself.

Stage 5g of docs/decompilation-process.md.

The SDK's error paths say who they are:

    "VIConfigure(): Tried to change mode from (%d) to (%d), which is forbidden"
    "OSCheckHeap: Failed 0 <= heap && heap < NumHeaps in %d"
    "__DSP_boot_task()  : IRAM MMEM ADDR: 0x%08X"

A function that holds a pointer to a string beginning with its own name is
that function. This is the same class of evidence as stage 5e's `__FILE__`,
and stronger in one way: it gives a NAME rather than a file.

TWO CHECKS, because a message is not proof on its own:

  - The name must exist in the SDK decomp. A message could be printed by a
    caller rather than by the function it names, and `VIConfigure` existing
    is what distinguishes a real SDK symbol from a fragment of prose that
    happens to parse as an identifier.
  - Exactly one function may reference the string. A message referenced from
    two places is not evidence about either.

Names are not invented from the message text: the string must yield an
identifier that the decomp also defines, so the result is a name the
community already uses for a function we located ourselves.
"""
import re, os, sys, argparse, collections

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import importlib.util
_spec = importlib.util.spec_from_file_location(
    "abs_", os.path.join(os.path.dirname(os.path.abspath(__file__)),
                         "attribute-by-strings.py"))
_abs = importlib.util.module_from_spec(_spec)
_spec.loader.exec_module(_abs)

C_DEF = re.compile(r'^(?:static\s+)?(?:inline\s+)?(?:extern\s+)?(?:asm\s+)?[A-Za-z_][\w\s\*]*?\b([A-Za-z_]\w*)\s*\([^;]*?\)\s*\{', re.M)

# A message that opens with an identifier, then `()`, `(` or `:`.
LEADING_NAME = re.compile(r'^(_{0,2}[A-Za-z][A-Za-z0-9_]{2,40})\s*(?:\(\s*\)|\(|:)')

def all_strings(path, is_rel, asm_files):
    """virtual address -> string, for every printable run in the image."""
    d, secs = (_abs.rel_sections(path, asm_files) if is_rel
               else _abs.dol_sections(path))
    out, start = {}, None
    for i, b in enumerate(d):
        if 0x20 <= b < 0x7F:
            if start is None:
                start = i
        else:
            if start is not None and b == 0 and i - start >= 6:
                va = _abs.to_vaddr(secs, start, is_rel)
                if va is not None:
                    out[va] = d[start:i].decode('ascii', 'replace')
            start = None
    return out

def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--image', required=True)
    ap.add_argument('--rel', action='store_true')
    ap.add_argument('--asm', nargs='+', required=True)
    ap.add_argument('--symbols', required=True)
    ap.add_argument('--boundaries', required=True)
    ap.add_argument('--sdk-src', nargs='+', required=True)
    ap.add_argument('--out')
    a = ap.parse_args()

    known = set()
    for root in a.sdk_src:
        for dirpath, _, files in os.walk(root):
            for f in files:
                if f.endswith(('.c', '.cpp')):
                    t = open(os.path.join(dirpath, f), errors='replace').read()
                    for m in C_DEF.finditer(t):
                        known.add(m.group(1))

    named, sizes = {}, {}
    for line in open(a.symbols):
        m = re.match(r'^\S+ 0x([0-9A-Fa-f]+) 0x\S+ (\S+)', line)
        if m: named[int(m.group(1), 16)] = m.group(2)
    taken = set(named.values())
    for line in open(a.boundaries):
        m = re.match(r'^\S+ = (\.\w+):0x([0-9A-Fa-f]+); // type:function size:0x([0-9A-Fa-f]+)', line)
        if m: sizes[int(m.group(2), 16)] = int(m.group(3), 16)

    strings = all_strings(a.image, a.rel, a.asm)
    candidates = {}
    for va, text in strings.items():
        m = LEADING_NAME.match(text)
        if m and m.group(1) in known:
            candidates[va] = m.group(1)
    print(f"messages opening with a known SDK function name: {len(candidates)}")

    refs, _imms = _abs.function_references(a.asm)
    users = collections.defaultdict(set)
    for fn, targets in refs.items():
        for t in targets:
            if t in candidates:
                users[t].add(fn)

    proposed, shared = {}, 0
    for va, name in candidates.items():
        who = users.get(va, set())
        if len(who) != 1:
            if who: shared += 1
            continue
        fn = next(iter(who))
        if fn in named or name in taken:
            continue
        proposed.setdefault(name, set()).add(fn)

    assigned = {}
    for name, fns in proposed.items():
        if len(fns) == 1:
            assigned[next(iter(fns))] = name

    print(f"referenced from exactly one function : {sum(len(v) for v in proposed.values())}")
    print(f"referenced from several (no evidence): {shared}")
    print(f"\nnamed {len(assigned)}")
    for fn in sorted(assigned):
        print(f"  0x{fn:08X}  {assigned[fn]}")

    if a.out and assigned:
        with open(a.out, 'w') as f:
            for fn in sorted(assigned):
                f.write(f".text 0x{fn:08X} 0x{sizes.get(fn, 0):X} "
                        f"{assigned[fn]} message\n")
        print(f"wrote {a.out}")

if __name__ == '__main__':
    main()
