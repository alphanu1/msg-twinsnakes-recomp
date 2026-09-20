#!/usr/bin/env python3
"""Phase 0 progress, measured five ways.

Run before updating HANDOFF.md so the figures there are computed rather than
remembered. "Percent decompiled" has no single honest answer for a
recompilation - we are not producing matching source - so five measures are
reported and averaged.

The average is an unweighted mean of five dissimilar measures. It is a
headline, not a statistic: read the rows.
"""
import re, collections, sys, os

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
def P(*p): return os.path.join(ROOT, *p)

def funcs(path):
    d = {}
    for l in open(path):
        m = re.match(r'^\S+ = (\.\w+):0x([0-9A-Fa-f]+); // type:function size:0x([0-9A-Fa-f]+)', l)
        if m: d[int(m.group(2), 16)] = int(m.group(3), 16)
    return d

def rows_for_readme(named, rel_named, dol, rel, sites, gx, ref_gx):
    total = len(dol) + len(rel)
    return [
        ("functions named", len(named.keys() & dol.keys()) +
                            len(rel_named.keys() & rel.keys()), total),
        ("function boundaries recovered", total, total),
        ("SDK entry points named", len([a for a in sites if a in named]), len(sites)),
        ("SDK call sites covered", sum(sites[a] for a in sites if a in named),
         sum(sites.values())),
        ("GX surface the game uses", len(gx), len(ref_gx)),
    ]


def main():
    dol = funcs(P('build/phase0/main.symbols.txt'))
    rel = funcs(P('build/phase0/rel.symbols.txt'))
    named = {}
    for l in open(P('config/symbols/main.dol.symbols.txt')):
        m = re.match(r'^\S+ 0x([0-9A-Fa-f]+) 0x\S+ (\S+)', l)
        if m: named[int(m.group(1), 16)] = m.group(2)

    # THE OVERLAY'S NAMES COUNT TOO, and only toward row 1.
    #
    # Row 1's denominator spans BOTH modules, so a numerator taken from
    # main.dol alone undercounts by however many REL symbols exist - it read
    # 997 while the maps held 1,006. Rows 3 and 4 are deliberately left on
    # main.dol: their call sites are the overlay calling INTO the DOL, so a
    # REL-local name is not an SDK entry point and must not inflate them.
    rel_named = {}
    for l in open(P('config/symbols/mgso_pal.rel.symbols.txt')):
        m = re.match(r'^\S+ 0x([0-9A-Fa-f]+) 0x\S+ (\S+)', l)
        if m: rel_named[int(m.group(1), 16)] = m.group(2)
    sites = collections.Counter()
    for l in open(P('build/phase0/out/mgso_pal/asm/auto_00_00000000_text.s')):
        for t in re.findall(r'\bbl fn_(80[0-9A-Fa-f]{6})', l):
            sites[int(t, 16)] += 1
    # THE GX SURFACE **THIS GAME CALLS**, not another game's.
    #
    # This row used to compare our count of GX-prefixed names against the
    # count in mkdd's symbol table, and a count is not a coverage: once four
    # more were named it read 179/177 = 101.1%. Two different games do not
    # have the same GX surface, and mkdd's table also carries C++ mangled
    # names of its own (`GXDrawBegin__14stParticleDrawFUl`), so the
    # denominator was never the right set to begin with.
    #
    # config/gx-surface-used.txt is that set: every GX function the engine
    # calls directly, recovered from the REL's cross-module calls. It is the
    # phase 3 specification, so measuring against it answers the question the
    # row is for - how much of the renderer's interface is understood.
    ref_gx = {line.split('#')[0].strip()
              for line in open(P('config/gx-surface-used.txt'))
              if line.split('#')[0].strip()}
    gx = ref_gx & set(named.values())

    # ---- the README's visual summary -------------------------------------
    #
    # A grid, generated rather than drawn. Rule 14 keeps the numeric table out
    # of anyone's hands for good reason - a remembered figure drifts and a
    # stale one is worse than none - and a picture of the same numbers earns
    # the same treatment. Every square below comes from the maps.
    #
    # The squares are grouped by SDK component because that is the boundary
    # the runtime has to replace: a component whose entry points are all known
    # can be shimmed, one with gaps cannot. Entry points still unnamed cannot
    # be attributed to a component - that is what being unnamed means - so
    # they are counted together rather than spread around to flatter the
    # picture.
    if '--readme' in sys.argv:
        import collections as _c
        known = _c.Counter(); unknown = 0
        # Prefixes in order: the longest first, so PSMTX is not eaten by PS.
        PFX = [('GX','GX'), ('OS','OS'), ('DC','OS'), ('IC','OS'),
               ('DVD','DVD'), ('CARD','CARD'), ('PAD','PAD'), ('VI','VI'),
               ('AX','audio'), ('AR','audio'), ('AI','audio'), ('DSP','audio'),
               ('EXI','EXI/SI'), ('SI','EXI/SI'),
               ('PS','maths'), ('MTX','maths'), ('C_','maths'), ('QUAT','maths')]
        for a_ in sites:
            nm = named.get(a_)
            if not nm:
                unknown += 1
                continue
            base = nm.lstrip('_')
            comp = 'other'
            for pfx, group in sorted(PFX, key=lambda x: -len(x[0])):
                if base.startswith(pfx):
                    comp = group; break
            known[comp] += 1

        rows = rows_for_readme(named, rel_named, dol, rel, sites, gx, ref_gx)
        print('<!-- progress:begin (generated by tools/progress.py --readme) -->')
        print()
        for label, got, of in rows:
            filled = int(round(20 * got / of)) if of else 0
            print(f'{"🟩" * filled}{"⬜" * (20 - filled)} **{label}** — '
                  f'{got:,} / {of:,} ({100*got/of:.1f}%)')
            print()
        print('**The SDK boundary, by component.** This is the surface the')
        print('runtime has to replace, so it is the measure that governs the')
        print('work. These are the entry points the engine actually calls,')
        print('and how many of each are identified:')
        print()
        print('| component | | identified |')
        print('|---|---|---|')
        for comp, n in known.most_common():
            print(f'| `{comp}` | {"🟩" * min(n, 30)}{f" +{n-30}" if n > 30 else ""} | {n} |')
        print(f'| *still unidentified* | {"⬜" * min(unknown, 30)}'
              f'{f" +{unknown-30}" if unknown > 30 else ""} | {unknown} |')
        print()
        print('An entry point that is not yet named cannot be attributed to a')
        print('component — that is what being unnamed means — so the last row')
        print('is counted together rather than spread across the others.')
        print()
        print('<!-- progress:end -->')
        return

    total = len(dol) + len(rel)
    rows = [
        ("Functions named",                          len(named.keys() & dol.keys()) +
                                                     len(rel_named.keys() & rel.keys()), total),
        ("Function boundaries recovered",            total, total),
        ("SDK entry points the engine calls, named", len([a for a in sites if a in named]), len(sites)),
        ("SDK call sites covered",                   sum(sites[a] for a in sites if a in named), sum(sites.values())),
        ("GX surface the game uses, named",          len(gx), len(ref_gx)),
    ]
    acc = sum(100 * a / b for _, a, b in rows)
    avg = acc / len(rows)

    if '--check' in sys.argv:
        sys.exit(check(rows, avg))

    print(f"| {'Measure':<42} | {'':>15} | {'':>6} |")
    print(f"|{'-'*44}|{'-'*17}|{'-'*8}|")
    for name, a, b in rows:
        print(f"| {name:<42} | {a:>6} / {b:<6} | {100*a/b:>5.1f}% |")
    print(f"| **{'Average of the five':<40}** | {'':>15} | **{avg:.1f}%** |")


def check(rows, avg):
    """Does HANDOFF.md's table still say what the evidence says?

    Rule 14 requires this table to be regenerated on every commit, and the
    reason it is a rule is that the failure is silent: commit 0f89358 added
    three symbols and left the table reading 961 when the map had become 964.
    Nothing was wrong with the symbols and nothing was wrong with the tool -
    the step was simply skipped, and a stale progress figure is worse than
    no progress figure because it is still believed.

    So the check is mechanical. It reads the numerator out of each row of
    HANDOFF.md's table and compares it with the number computed here.
    """
    handoff = P('HANDOFF.md')
    text = open(handoff).read()
    bad = []

    for name, a, b in rows:
        m = re.search(r'^\| ' + re.escape(name) + r' \| *([\d,]+) */', text, re.M)
        if not m:
            bad.append(f"  {name}: no such row in HANDOFF.md")
        elif int(m.group(1).replace(',', '')) != a:
            bad.append(f"  {name}: HANDOFF says {m.group(1)}, evidence says {a:,}")

    m = re.search(r'^## PHASE 0 PROGRESS . ([\d.]+)%', text, re.M)
    if not m:
        bad.append("  headline: no '## PHASE 0 PROGRESS' line in HANDOFF.md")
    elif abs(float(m.group(1)) - avg) > 0.05:
        bad.append(f"  headline: HANDOFF says {m.group(1)}%, evidence says {avg:.1f}%")

    # MILESTONES.md carries two of the same numbers and drifts the same way -
    # its symbol total was 92 behind the maps when this check was written,
    # which is further than HANDOFF ever got because nothing recomputed it.
    total_symbols = sum(
        sum(1 for line in open(P('config/symbols', f))
            if re.match(r'^\S+ 0x[0-9A-Fa-f]+ \S+ \S+ \S+\s*$', line))
        for f in ('main.dol.symbols.txt', 'mgso_pal.rel.symbols.txt'))

    ms = open(P('MILESTONES.md')).read()
    m = re.search(r'^\*\*Phase 0, in progress . ([\d,]+) symbols', ms, re.M)
    if not m:
        bad.append("  symbols: no 'Phase 0, in progress - N symbols' line in MILESTONES.md")
    elif int(m.group(1).replace(',', '')) != total_symbols:
        bad.append(f"  symbols: MILESTONES says {m.group(1)}, "
                   f"the maps hold {total_symbols:,}")

    m = re.search(r'Phase 0 progress is ([\d.]+)%', ms)
    if not m:
        bad.append("  headline: no 'Phase 0 progress is N%' line in MILESTONES.md")
    elif abs(float(m.group(1)) - avg) > 0.05:
        bad.append(f"  headline: MILESTONES says {m.group(1)}%, evidence says {avg:.1f}%")

    if bad:
        print("The recorded progress figures are stale (project rule 14):")
        print('\n'.join(bad))
        print("\nRegenerate it:  python3 tools/progress.py")
        return 1

    print(f"HANDOFF.md and MILESTONES.md agree with the evidence "
          f"({total_symbols:,} symbols, {avg:.1f}%).")
    return 0


if __name__ == '__main__':
    main()
