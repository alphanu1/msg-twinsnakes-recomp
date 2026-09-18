# Host

The executable that runs the game under **our** runtime.

Phase 1 borrowed ModernGekko — a Dolphin-derived runtime — to prove the
translated CPU code was correct before any of our own code could be blamed for
a failure. This replaces it. Nothing here links ModernGekko or Dolphin.

## What it does

1. Allocates guest memory (24 MB MEM1, 16 MB ARAM).
2. Mounts the user's disc.
3. `dlopen`s the recompiled module and takes its dispatch entry point.
4. Installs the patch table, so SDK calls reach our native implementations
   instead of their translated bodies.
5. Runs from the module's entry point and reports where it gets to.

## What it deliberately does not do yet

No window and no rendering. Phase 2's exit criterion is explicitly headless —
the game runs its main loop, reads assets, responds to input, and its
`OSReport` output matches Dolphin's. GX calls are logged and discarded until
phase 3.

That ordering is not caution. Until the OS and DVD shims are right, a renderer
would be debugging two unknowns at once.
