#!/usr/bin/env python3
"""Make the patch table work for intra-chunk calls.

THE PROBLEM. DolRecomp consults dolrecomp_dispatch_replacement only on the
dispatch path. A call whose target lives in the SAME generated chunk compiles
to a plain `goto label_XXXXXXXX;`, which never consults it. So a patched SDK
function is silently bypassed whenever its caller happens to share a chunk -
which has already blocked ICFlashInvalidate, DCFlushRange and
__OSInitAudioSystem, and which every future shim inherits.

THE FIX. Insert a guard at the top of each patched function's label:

    label_8001D184:
        if (dolrecomp_dispatch_replacement(ctx, 0x8001D184u)) return;
        ctx->pc = 0x8001D184u;
        ...

Reaching that label by ANY route - goto, fallthrough or dispatch - now offers
the address to the patch table first. Returning is correct because the patch
handler sets pc to lr before it returns 1, so the host re-dispatches at the
caller's resume address, which is exactly what the function's own `blr` would
have done.

WHY POST-PROCESSING RATHER THAN A DOLRECOMP CHANGE. Project rule 4 keeps
extern/ unmodified, and carrying a patch against an upstream for the life of
the project is worse than owning a transform over our own build output. The
generated C is a build artefact regenerated from the user's disc on every
build, so rewriting it changes nothing that is committed and nothing that is
upstream.
"""
import re, sys, argparse, pathlib

LABEL = re.compile(r'^label_([0-9A-Fa-f]{8}):\s*$')

def patched_addresses(symbols_path, implemented_path):
    names = set()
    for line in open(implemented_path):
        name = line.split('#')[0].strip()
        if name:
            names.add(name)
    out = {}
    for line in open(symbols_path):
        m = re.match(r'^(\.\w+) 0x([0-9A-Fa-f]+) \S+ (\S+)', line)
        if m and m.group(1) == '.text' and m.group(3) in names:
            out[int(m.group(2), 16)] = m.group(3)
    return out

def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--chunks', required=True, help='directory of generated chunk .c files')
    ap.add_argument('--symbols', required=True)
    ap.add_argument('--implemented', required=True)
    ap.add_argument('--dry-run', action='store_true')
    a = ap.parse_args()

    want = patched_addresses(a.symbols, a.implemented)
    if not want:
        print("no patched addresses; nothing to do")
        return 0

    files = sorted(pathlib.Path(a.chunks).glob('*.c'))
    injected, already, touched = 0, 0, 0

    for path in files:
        lines = open(path).read().split('\n')
        out, changed = [], False
        i = 0
        while i < len(lines):
            out.append(lines[i])
            m = LABEL.match(lines[i].strip())
            if m:
                addr = int(m.group(1), 16)
                if addr in want:
                    # Idempotent: re-running must not stack guards, because the
                    # build regenerates chunks and this runs on every build.
                    #
                    # LOOK PAST THE COMMENT. The guard is written as two lines
                    # - a comment naming the function, then the call - so
                    # checking only the next line always saw the comment,
                    # never the call, and re-injected every time. The chunks
                    # carried 104 guard sites for 35 patched functions:
                    # duplicates stacked by successive runs. Harmless at
                    # runtime, since the second call only runs when the first
                    # returned 0, but this file claims to be idempotent and
                    # was not.
                    nxt = '\n'.join(lines[i + 1:i + 3])
                    if 'dolrecomp_dispatch_replacement' in nxt:
                        already += 1
                    else:
                        out.append(
                            f'    /* patch guard: {want[addr]} */'
                        )
                        out.append(
                            f'    if (dolrecomp_dispatch_replacement(ctx, 0x{addr:08X}u)) return;'
                        )
                        injected += 1
                        changed = True
            i += 1
        if changed and not a.dry_run:
            open(path, 'w').write('\n'.join(out))
            touched += 1

    print(f"patch guards: {injected} injected, {already} already present, "
          f"{touched} files rewritten, {len(want)} patched functions known")
    if injected == 0 and already == 0:
        print("  WARNING: none of the patched functions have a label in these "
              "chunks - wrong directory, or the addresses are in another module")
    return 0

if __name__ == '__main__':
    sys.exit(main())
