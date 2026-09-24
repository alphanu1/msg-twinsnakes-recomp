/* A SAMPLING PROFILER, BECAUSE GUESSING WAS COSTING MORE THAN BUILDING ONE.
 *
 * Three rounds of inner-loop micro-optimisation bought about 6% between them,
 * which is what happens when the hot code is chosen by reading rather than by
 * measuring. perf is not available here and the machine is not always idle,
 * so wall-clock ablation is noisy as well as indirect.
 *
 * SIGPROF fires on the process CPU clock, so time spent descheduled while
 * another process runs is NOT sampled - which is exactly the property the
 * wall-clock measurements lacked. The handler stores the interrupted program
 * counter and nothing else; resolving an address to a function needs a symbol
 * table, which belongs offline, not in a signal handler.
 *
 * MGS_PROFILE=1 to enable.
 */
/* REG_RIP lives behind _GNU_SOURCE, and ucontext_t must come before use. */
#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif
#include <ucontext.h>

#include "profile.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <sys/time.h>

#define PROF_SLOTS 65536u

static unsigned long  prof_pc[PROF_SLOTS];   /* interrupted address */
static unsigned long  prof_hits[PROF_SLOTS];
static volatile unsigned long prof_total;
static volatile unsigned long prof_lost;
static int            prof_on;
static unsigned long  prof_base;             /* load address of the binary */

/* Open addressing. The handler must not allocate, lock or call anything that
 * is not async-signal-safe, so the table is fixed and collisions probe. */
static void prof_record(unsigned long pc)
{
    unsigned long h = (pc >> 2) * 2654435761u;
    unsigned i, slot;
    for (i = 0; i < 16u; ++i) {
        slot = (unsigned)((h + i) & (PROF_SLOTS - 1u));
        if (prof_hits[slot] == 0ul) { prof_pc[slot] = pc; prof_hits[slot] = 1ul; return; }
        if (prof_pc[slot] == pc)    { prof_hits[slot] += 1ul; return; }
    }
    prof_lost += 1ul;
}

static void prof_tick(int sig, siginfo_t* si, void* uc)
{
    ucontext_t* c = (ucontext_t*)uc;
    (void)sig; (void)si;
    prof_total += 1ul;
#if defined(__x86_64__)
    prof_record((unsigned long)c->uc_mcontext.gregs[REG_RIP]);
#elif defined(__aarch64__)
    prof_record((unsigned long)c->uc_mcontext.pc);
#else
    (void)c;
#endif
}

/* The binary is position-independent, so a sampled address means nothing
 * without the address it was loaded at. Read it once, at start-up. */
static void prof_find_base(void)
{
    FILE* f = fopen("/proc/self/maps", "r");
    char line[512];
    if (!f) return;
    if (fgets(line, sizeof line, f)) sscanf(line, "%lx", &prof_base);
    fclose(f);
}

void mgs_profile_start(void)
{
    struct sigaction sa;
    struct itimerval it;
    const char* e = getenv("MGS_PROFILE");
    if (!e || !*e || *e == '0') return;

    prof_find_base();

    memset(&sa, 0, sizeof sa);
    sa.sa_sigaction = prof_tick;
    sa.sa_flags = SA_SIGINFO | SA_RESTART;
    sigemptyset(&sa.sa_mask);
    if (sigaction(SIGPROF, &sa, NULL) != 0) return;

    /* 1 kHz of process CPU time. Fast enough to resolve an inner loop over a
     * 40-second run, slow enough that the handler is not itself the cost. */
    it.it_interval.tv_sec = 0; it.it_interval.tv_usec = 1000;
    it.it_value = it.it_interval;
    if (setitimer(ITIMER_PROF, &it, NULL) != 0) return;

    prof_on = 1;
    fprintf(stderr, "[profile] sampling at 1 kHz of CPU time, base 0x%lx\n", prof_base);
}

static int prof_cmp(const void* a, const void* b)
{
    unsigned ia = *(const unsigned*)a, ib = *(const unsigned*)b;
    if (prof_hits[ia] < prof_hits[ib]) return 1;
    if (prof_hits[ia] > prof_hits[ib]) return -1;
    return 0;
}

void mgs_profile_report(void)
{
    unsigned order[PROF_SLOTS];
    unsigned n = 0, i;
    struct itimerval off;

    if (!prof_on) return;
    memset(&off, 0, sizeof off);
    setitimer(ITIMER_PROF, &off, NULL);
    prof_on = 0;

    for (i = 0; i < PROF_SLOTS; ++i) if (prof_hits[i]) order[n++] = i;
    qsort(order, n, sizeof order[0], prof_cmp);

    fprintf(stderr, "[profile] %lu samples over %u addresses (%lu lost to collisions)\n",
            prof_total, n, prof_lost);
    fprintf(stderr, "[profile] base 0x%lx - subtract it to look an address up\n", prof_base);

    /* WHERE THE TIME GOES, OVER EVERY SAMPLE AND NOT JUST THE TOP FORTY.
     *
     * The list below is capped at forty lines, and in this program the top
     * forty are a fifth of the samples: the rest is spread across twenty
     * thousand addresses in the translated game code, a fraction of a per
     * cent each. Summing the printed lines therefore answers "which
     * function is hottest" and cannot answer "is the cost the game's code
     * or ours", which is the question that decides what to work on.
     *
     * The split is by ADDRESS RANGE. Our binary is loaded at `prof_base`
     * and is a few megabytes; the recompiled module is dlopened far away.
     * Anything within 64 MB of the base is ours, anything else is the
     * game's - crude, and exact enough for a ratio. */
    {
        unsigned long ours = 0ul, theirs = 0ul;
        unsigned k;
        for (k = 0; k < PROF_SLOTS; ++k) {
            if (!prof_hits[k]) continue;
            if (prof_pc[k] >= prof_base &&
                prof_pc[k] - prof_base < (64ul << 20))
                ours += prof_hits[k];
            else
                theirs += prof_hits[k];
        }
        if (prof_total) {
            fprintf(stderr, "[profile] ALL SAMPLES: translated game code "
                            "%5.1f%%, our native runtime %5.1f%%\n",
                    100.0 * (double)theirs / (double)prof_total,
                    100.0 * (double)ours / (double)prof_total);
        }
    }
    /* Offsets, not absolute addresses: an offset can be fed straight to
     * addr2line against the binary, which is the point of printing them. */
    for (i = 0; i < n && i < 40u; ++i) {
        unsigned s = order[i];
        fprintf(stderr, "[profile] %6.2f%%  %8lu  +0x%lx\n",
                100.0 * (double)prof_hits[s] / (double)(prof_total ? prof_total : 1ul),
                prof_hits[s], prof_pc[s] - prof_base);
    }
}
