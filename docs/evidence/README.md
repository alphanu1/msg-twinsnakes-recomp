# Analysis evidence

Machine-produced records of the analysis this project's symbol map rests on.
Regenerate any of it with `tools/ghidra-analyse.sh <binary> <name>`.

| File | What it is |
|---|---|
| `*.functions.txt` | Function inventory: address, size, name, incoming-call count |
| `*.analysis.log` | The full Ghidra headless run — what was loaded, which processor, what it did |
| `*.sha256` | Hashes of the input binary and the inventory, so a re-run can be compared |

## What this is for

Two things. It **cross-checks** the symbol map against an analyser that shares
no code with `decomp-toolkit`, and it leaves an **auditable record** that the
analysis happened as described.

| | Ghidra | `dtk` | agreement |
|---|---|---|---|
| `main.dol` functions | 1,687 | 1,818 | 92.8% |
| `mgso_pal.rel` functions | 16,323 | 16,667 | **97.9%** |

Of 773 named symbols, Ghidra confirms 606 at the same addresses. The 167 it
does not are data symbols — `__GXData`, `__PADSpec`, `__DVDVersion` — which it
correctly declines to call functions.

## What is deliberately absent

**Decompiled source.** Ghidra's pseudo-C is a reconstruction of the game's own
code. `Sega v. Accolade` and `Sony v. Connectix` protect the *act* of
decompiling, and both turned on the intermediate copies not being distributed —
so the case law protects producing this, not publishing it. See
[../legal-position.md](../legal-position.md).

Everything here is **facts**: addresses, sizes, names, call counts. The same
class of data as the symbol maps every decompilation project publishes, and as
`doldecomp/mkdd`'s `symbols.txt`, which stage 5 depends on.

The Ghidra project and any decompiler output stay under `build/`, which is
git-ignored. The committed hashes are what make a run checkable without
distributing anything: re-run the command, compare.
