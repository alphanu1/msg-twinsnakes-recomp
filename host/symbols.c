/* Names for guest addresses, from config/symbols/.
 *
 * WHY THIS EXISTS. Every dump in this host printed bare addresses, and
 * `mgs_dump_threads` was written with a `symbol` callback that nothing ever
 * passed - so the hook was there and the names were not. Reading a task
 * table of twelve raw pointers means resolving each one by hand against the
 * disassembly, which is the step that made "the renderer stopped" expensive
 * to turn into a specific question.
 *
 * TWO ADDRESS SPACES. main.dol runs at its link address, so its symbols are
 * absolute and a lookup is a plain range test. The overlay is a REL that
 * OSLink places wherever the heap had room, so its symbols are OFFSETS and
 * the base has to come from the module header at run time - the same section
 * table `mgs_clear_overlay_bss` reads, for the same reason: it follows the
 * disc rather than a constant compiled in here.
 *
 * The names themselves carry an origin per project rule 15; this file only
 * reads them, and treats a map it cannot parse as no map at all rather than
 * inventing a name for an address.
 */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "module.h"

typedef struct {
    uint32_t value;      /* address for the DOL, section offset for the REL */
    uint32_t size;       /* 0 when the map says "?" */
    char     name[64];
} sym_t;

static sym_t* s_dol;
static size_t s_dol_n;
static sym_t* s_rel;
static size_t s_rel_n;
static uint32_t s_rel_text;      /* guest address of REL .text offset 0 */

static int by_value(const void* a, const void* b)
{
    uint32_t x = ((const sym_t*)a)->value, y = ((const sym_t*)b)->value;
    return x < y ? -1 : x > y ? 1 : 0;
}

/* Format: <section> <value> <size> <name> <origin>, '#' comments, blanks. */
static size_t load(const char* path, sym_t** out, const char* section)
{
    FILE* fh = fopen(path, "r");
    char line[512];
    size_t n = 0, cap = 0;
    sym_t* v = NULL;

    if (!fh) return 0;
    while (fgets(line, sizeof line, fh)) {
        char sec[32], val[32], size[32], name[64];
        if (line[0] == '#' || line[0] == '\n') continue;
        if (sscanf(line, "%31s %31s %31s %63s", sec, val, size, name) != 4)
            continue;
        if (section && strcmp(sec, section) != 0) continue;
        if (n == cap) {
            cap = cap ? cap * 2 : 256;
            v = (sym_t*)realloc(v, cap * sizeof *v);
            if (!v) { fclose(fh); *out = NULL; return 0; }
        }
        v[n].value = (uint32_t)strtoul(val, NULL, 0);
        v[n].size = size[0] == '?' ? 0u : (uint32_t)strtoul(size, NULL, 0);
        snprintf(v[n].name, sizeof v[n].name, "%s", name);
        ++n;
    }
    fclose(fh);
    if (v) qsort(v, n, sizeof *v, by_value);
    *out = v;
    return n;
}

void mgs_symbols_load(const char* dir);
void mgs_symbols_load(const char* dir)
{
    char path[512];
    snprintf(path, sizeof path, "%s/main.dol.symbols.txt", dir);
    s_dol_n = load(path, &s_dol, NULL);
    snprintf(path, sizeof path, "%s/mgso_pal.rel.symbols.txt", dir);
    s_rel_n = load(path, &s_rel, ".text");
    printf("symbols: %zu from main.dol, %zu from mgso_pal.rel .text\n",
           s_dol_n, s_rel_n);
}

/* The overlay's .text base, read from the module header in guest memory.
 *
 * REL section 0 is always the null section; .text is the first with a
 * non-zero offset and the executable flag in bit 0 of the offset word. That
 * flag bit is part of the stored offset and must be masked off, which is the
 * kind of detail that silently shifts every name by one or two bytes. */
#define REL_NUM_SECTIONS 0x0Cu
#define REL_SECTION_INFO 0x10u

void mgs_symbols_set_overlay(void* cpu, uint32_t module);
void mgs_symbols_set_overlay(void* cpu, uint32_t module)
{
    uint32_t count = mgs_module_guest_read32(cpu, module + REL_NUM_SECTIONS);
    uint32_t info  = mgs_module_guest_read32(cpu, module + REL_SECTION_INFO);
    uint32_t i;

    s_rel_text = 0u;
    if (info && info < 0x1000000u) info += module;   /* not yet relocated */
    if (!count || count > 64u) return;

    for (i = 1u; i < count; ++i) {
        uint32_t off = mgs_module_guest_read32(cpu, info + i * 8u);
        uint32_t size = mgs_module_guest_read32(cpu, info + i * 8u + 4u);
        int executable = (off & 1u) != 0u;
        off &= ~3u;
        if (!off || !size || !executable) continue;
        if (off < 0x1000000u) off += module;
        s_rel_text = off;
        break;
    }
    printf("symbols: overlay .text at 0x%08X\n", s_rel_text);
}

static const char* search(const sym_t* v, size_t n, uint32_t key,
                          char* out, size_t out_size)
{
    size_t lo = 0, hi = n;
    while (lo < hi) {                       /* last symbol with value <= key */
        size_t mid = lo + (hi - lo) / 2;
        if (v[mid].value <= key) lo = mid + 1; else hi = mid;
    }
    if (!lo) return NULL;
    --lo;
    /* A sized symbol that does not reach the address is not the answer: the
     * gaps between named functions are full of unnamed ones, and reporting
     * the previous name for them is worse than reporting nothing. */
    if (v[lo].size && key >= v[lo].value + v[lo].size) return NULL;
    if (!v[lo].size && key - v[lo].value > 0x1000u) return NULL;
    if (key == v[lo].value)
        snprintf(out, out_size, "%s", v[lo].name);
    else
        snprintf(out, out_size, "%s+0x%X", v[lo].name, key - v[lo].value);
    return out;
}

const char* mgs_symbol_for(uint32_t addr);
const char* mgs_symbol_for(uint32_t addr)
{
    /* A small ring, so several can appear in one printf without the second
     * overwriting the first - which is exactly how a dump ends up claiming
     * two different addresses have the same name. */
    static char ring[4][96];
    static unsigned slot;
    char* out = ring[slot++ & 3u];
    const char* got = NULL;

    if (!addr) return NULL;
    if (s_dol_n) got = search(s_dol, s_dol_n, addr, out, sizeof ring[0]);
    if (!got && s_rel_n && s_rel_text && addr >= s_rel_text)
        got = search(s_rel, s_rel_n, addr - s_rel_text, out, sizeof ring[0]);
    return got;
}
