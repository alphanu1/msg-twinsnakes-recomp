# Module glue

Twin Snakes keeps 92% of its code in `mgso_pal.rel`, and ModernGekko's
`moderngekko-port` recompiles `main.dol` only — nothing in that toolchain
populates the `rel_modules` field its own ABI defines. This directory supplies
the missing piece.

It lives here rather than in `extern/` because project rule 4 keeps upstream
unmodified, and because carrying a patch against two upstreams for the life of
the project is worse than owning a hundred lines of glue.

## How it works

DolRecomp emits, per module, a `dolrecomp_find_original(address)` lookup over
that module's own chunk table, and calls it from `dolrecomp_call`. Two modules
therefore have two tables that do not know about each other.

`dolrecomp_call` consults an extension point **first**:

```c
static inline int dolrecomp_call(CPUState* ctx, u32 address) {
    ctx->pc = address;
    if (dolrecomp_dispatch_replacement(ctx, address)) return 1;   /* <- here */
    ...
    if (dolrecomp_call_original(ctx, address)) return 1;          /* own table */
```

With `DOLRECOMP_ENABLE_REPLACEMENTS` defined, that becomes an external function
for us to supply. So:

| file | role |
|---|---|
| `dol_bridge.c` | includes the DOL's `generated.h`, exposes its table as `mgs_dol_call` |
| `rel_bridge.c` | includes the REL's `generated.h`, exposes its table as `mgs_rel_call` |
| `dispatch.c` | implements `dolrecomp_dispatch_replacement`, routing by address |

Separate translation units are not a style choice: both headers define
`dolrecomp_find_original` and friends as `static inline` over *their own*
tables, so including both in one file would collide.

**No recursion.** Each bridge calls `dolrecomp_call_original` — the table
lookup — not `dolrecomp_call`, so a cross-module call resolves in one hop
rather than bouncing back through the router.

This is the same hook phase 2 will use to route SDK functions to native
implementations, so the shape is worth getting right now.
