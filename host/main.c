/* Run the recompiled game under our own runtime.
 *
 * Headless by design: phase 2's exit criterion is the main loop running,
 * assets loading and OSReport matching Dolphin, with GX logged and discarded.
 * A window would only add a second unknown while the OS and DVD shims are
 * still being proven.
 */
#include "memory/guest.h"
#include "dvd/disc.h"
#include "dvd/disc_locate.h"
#include "dvd/dvd.h"
#include "dvd/dol.h"
#include "os/os_runtime.h"
#include "platform/profile.h"
#include "os/patch_table.h"
#include "platform/jobs.h"
#include "module.h"
#include "platform/sdl_video.h"
#include "platform/mmio.h"
#include "gx/efb.h"
#include "gx/raster.h"
#include "gx/fifo.h"
#include <SDL3/SDL.h>

void mgs_dvd_service(const MgsModule* mod, void* cpu, MgsDvd* dvd);
uint64_t mgs_dvd_completed(void);
uint64_t mgs_dvd_bytes(void);
uint64_t mgs_dvd_callbacks(void);
const MgsEfb* mgs_display_efb(void);
const MgsGx* mgs_display_gx(void);
const MgsGxRaster* mgs_display_raster(void);

static void dvd_pump(const MgsModule* mod, void* cpu, void* user)
{
    mgs_dvd_service(mod, cpu, (MgsDvd*)user);
}

/* The run loop drives the graphics copy and the presentation; both need guest
 * memory, which lives in main's frame. Bound once at startup. */
static GuestMemory* s_display_mem;
static int          s_display_windowed;

static void display_pump(void)
{
    mgs_display_service(mgs_host_mmio(), s_display_mem, MGS_XFB_HEIGHT);
}

/* One retrace. Present what the video interface is scanning; if there is
 * nothing there yet, leave the boot overlay up rather than replace it with a
 * black rectangle that says less. */
/* Read a NUL-terminated guest string. Byte by byte through the swapping
 * accessor rather than by casting: the guest is big-endian. */
static void guest_string(void* cpu, uint32_t addr, char* out, unsigned n)
{
    unsigned i;
    out[0] = '\0';
    if (!addr) return;
    for (i = 0; i + 1u < n; ++i) {
        uint32_t w = mgs_module_guest_read32(cpu, addr + (i & ~3u));
        char c = (char)((w >> (8u * (3u - (i & 3u)))) & 0xFFu);
        if (!c) break;
        out[i] = c;
    }
    out[i] = '\0';
}

/* fn_8004E7BC(size, mustSucceed, file, line) - the game's tracking allocator,
 * and fn_8004E830(ptr, file, line) - the matching free. Logged together
 * because an arena that runs out is a question about the PAIR: what was taken
 * and what was given back. */
static void trace_alloc(void* cpu, const uint32_t* gpr)
{
    char file[64];
    guest_string(cpu, gpr[5], file, sizeof file);
    fprintf(stderr, "[alloc] %9u bytes  must=%u  %s:%u\n",
            gpr[3], gpr[4], file, gpr[6]);
}

static void trace_free(void* cpu, const uint32_t* gpr)
{
    char file[64];
    guest_string(cpu, gpr[4], file, sizeof file);
    fprintf(stderr, "[free ] 0x%08X                %s:%u\n",
            gpr[3], file, gpr[5]);
}

/* OSCreateHeap(lo, hi) - the bounds every later allocation comes out of.
 * "The arena is 21 MB" says nothing if the heap carved from it is smaller. */
static void trace_createheap(void* cpu, const uint32_t* gpr)
{
    (void)cpu;
    fprintf(stderr, "[heap] OSCreateHeap(0x%08X, 0x%08X) = %u bytes\n",
            gpr[3], gpr[4], gpr[4] - gpr[3]);
}

/* OSSetArenaLo(lo) - every move of the arena's floor, and WHO moved it.
 *
 * The allocator that calls this is a leaf reached inside a chunk, so the host
 * never sees its address; the stack does. PowerPC frames are a linked list -
 * each frame's first word is the previous frame, and its second word is the
 * return address into the caller - so walking it names the subsystem that
 * took the memory, which is the only part that matters here. */
static void trace_setarenalo(void* cpu, const uint32_t* gpr)
{
    static uint32_t last;
    uint32_t frame = gpr[1];
    unsigned depth;

    if (gpr[3] > last && last)
        fprintf(stderr, "[arena] lo -> 0x%08X  (+%u bytes)\n", gpr[3], gpr[3] - last);
    else
        fprintf(stderr, "[arena] lo -> 0x%08X\n", gpr[3]);
    last = gpr[3];

    fprintf(stderr, "        callers:");
    for (depth = 0; depth < 5u && frame >= 0x80000000u && frame < 0x81800000u; ++depth) {
        uint32_t ret = mgs_module_guest_read32(cpu, frame + 4u);
        if (ret) fprintf(stderr, " 0x%08X", ret);
        frame = mgs_module_guest_read32(cpu, frame);
    }
    fprintf(stderr, "\n");
}

/* What the heartbeat reports: 0 = framebuffer copies, 1 = disc reads. */
static uint64_t progress_counter(unsigned which)
{
    const MgsEfb* e = mgs_display_efb();
    return which == 0u ? e->copies : mgs_dvd_completed();
}

/* Set when the user closed the window or pressed Escape: the run stops, and
 * the post-run viewer is skipped so one close means closed. */
static int s_quit_requested;

static void frame_pump(void)
{
    static uint64_t shown = ~0ull;
    uint64_t copies;

    if (!s_display_windowed) return;

    /* Input has to stay responsive on every tick; the picture only needs
     * redrawing when the game has actually finished one. The copy counter is
     * the honest signal for that - it is what the game does to put a frame in
     * the external framebuffer.
     *
     * A QUIT HERE MUST ACTUALLY QUIT. This used to discard the pump's result,
     * so Escape and the window's close button were polled and thrown away for
     * as long as the guest ran - and with a large step budget that is
     * forever. The window could not be closed by any normal means. Stopping
     * the guest is what the interrupt flag already means, so reuse it rather
     * than invent a second way to stop. */
    if (!mgs_video_pump()) {
        s_quit_requested = 1;
        mgs_module_interrupted = 1;
        return;
    }

    /* Two signals, not one. The copy counter catches GX drawing a frame;
     * the scan-out address catches anything that writes the external
     * framebuffer directly and just flips to it, which is how a video
     * decoder can present without GX copying anything. Keying on the copy
     * alone would freeze such a picture. */
    copies = mgs_display_efb()->copies
           ^ ((uint64_t)mgs_mmio_xfb_address(mgs_host_mmio()) << 32);
    if (copies == shown) return;
    shown = copies;

    if (mgs_display_present(mgs_host_mmio(), s_display_mem))
        mgs_video_present();
}


#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <execinfo.h>
#include <stdarg.h>

static void overlay_line(const char* fmt, ...);

static void report_to_stdout(void* user, const char* line)
{
    FILE* out = (FILE*)user;
    /* The game's own messages, kept distinguishable from ours: the phase 1
     * harness diffs these against Dolphin's, so they must not be interleaved
     * with host chatter in a way that a diff would trip over.
     */
    overlay_line("OSREPORT: %.40s", line);
    fprintf(out, "[OSReport] %s", line);
    if (!*line || line[strlen(line) - 1] != '\n') fputc('\n', out);
    fflush(out);
}

/* The boot overlay.
 *
 * The screen is the only output channel that survives into a shipped build: a
 * terminal is there now and will not be later, and a boot that fails in front
 * of a black window tells you nothing. So the window shows real state from the
 * first frame, and game output replaces it when there is any.
 */
#define OVERLAY_LINES 14
static char s_overlay[OVERLAY_LINES][64];
static int  s_overlay_used;

static void overlay_line(const char* fmt, ...)
{
    va_list ap;
    if (s_overlay_used >= OVERLAY_LINES) {
        memmove(s_overlay[0], s_overlay[1], sizeof s_overlay - sizeof s_overlay[0]);
        --s_overlay_used;
    }
    va_start(ap, fmt);
    vsnprintf(s_overlay[s_overlay_used], sizeof s_overlay[0], fmt, ap);
    va_end(ap);
    ++s_overlay_used;
}

static void overlay_draw(int running)
{
    int i;
    mgs_video_clear(0x00101018u);
    mgs_video_rect(0, 0, MGS_XFB_WIDTH, 20, 0x00202838u);
    mgs_video_text(8, 6, 0x00E0E0F0u, "MGS TWIN SNAKES - NATIVE PORT");
    mgs_video_text(MGS_XFB_WIDTH - 130, 6,
                   running ? 0x0060E060u : 0x00E06060u,
                   running ? "RUNNING" : "STOPPED");
    for (i = 0; i < s_overlay_used; ++i)
        mgs_video_text(8, 32 + i * 10, 0x00C0C8D0u, s_overlay[i]);
    mgs_video_rect(0, MGS_XFB_HEIGHT - 16, MGS_XFB_WIDTH, 16, 0x00181C24u);
    mgs_video_text(8, MGS_XFB_HEIGHT - 12, 0x00808890u, "ESC TO QUIT");
}

/* The patch table, as the module's dispatch hook sees it. Returns 1 when a
 * native implementation ran, which tells the module not to execute the
 * translated body.
 */
static unsigned long s_patched_calls;

/* Per-address call counts for the patch table.
 *
 * A single total says the shims are being used; it does not say WHICH, and
 * "8 million native calls" turned out to be almost entirely
 * OSDisableInterrupts. Knowing that DVDOpen was called zero times is what
 * distinguishes "the game is loading" from "the game has decided not to". */
#define PATCH_COUNTS 64
static struct { uint32_t addr; uint64_t calls; } s_patch_counts[PATCH_COUNTS];

static void patch_count(uint32_t address)
{
    unsigned i;
    for (i = 0; i < PATCH_COUNTS; ++i) {
        if (s_patch_counts[i].addr == address) { ++s_patch_counts[i].calls; return; }
        if (s_patch_counts[i].addr == 0u) {
            s_patch_counts[i].addr = address;
            s_patch_counts[i].calls = 1u;
            return;
        }
    }
}

static void patch_report(void)
{
    unsigned i, k;
    printf("native SDK calls by function:\n");
    for (k = 0; k < 12u; ++k) {
        unsigned best = PATCH_COUNTS; uint64_t bestc = 0u;
        for (i = 0; i < PATCH_COUNTS; ++i)
            if (s_patch_counts[i].addr && s_patch_counts[i].calls > bestc) {
                bestc = s_patch_counts[i].calls; best = i;
            }
        if (best == PATCH_COUNTS) break;
        printf("  0x%08X  %12llu\n", s_patch_counts[best].addr,
               (unsigned long long)bestc);
        s_patch_counts[best].calls = 0u;
    }
}

/* MGS_NO_MEM_SHIM runs the TRANSLATED memcpy/memset/__fill_mem instead of
 * the native ones, so the two can be compared without rebuilding.
 *
 * Worth having as a switch rather than a build flag: these three account for
 * 86% of the boot, so making them native changes how far the game gets in a
 * given number of steps by a large factor, and any difference in behaviour
 * after that needs to be attributable to the shim or cleared of it. Being
 * able to run the same binary both ways is what makes that a measurement
 * rather than an argument. */
static int mem_shim_disabled(void)
{
    static int cached = -1;
    if (cached < 0) cached = getenv("MGS_NO_MEM_SHIM") != NULL;
    return cached;
}

static int mgs_host_patch_dispatch(void* cpu_state, uint32_t address)
{
    MgsSdkFn fn;

    if (mem_shim_disabled() &&
        (address == 0x800050B4u || address == 0x800050E4u ||
         address == 0x8000519Cu))
        return 0;

    fn = mgs_patch_lookup(address);
    if (!fn) return 0;

    patch_count(address);

    fn((CPUState*)cpu_state);

    /* A native replacement stands in for a function that ended in `blr`, so
     * it must RETURN: the translated caller left its resume address in lr and
     * expects control there. Without this the pc never moves, the host
     * re-dispatches the same address forever, and it looks exactly like the
     * guest spinning - which is how this first presented, at
     * ICFlashInvalidate, after 1002 otherwise correct native calls.
     */
    mgs_module_set_pc(cpu_state, mgs_module_lr(cpu_state));

    ++s_patched_calls;
    return 1;
}

static unsigned long mgs_host_patched_calls(void) { return s_patched_calls; }

/* A SECOND signal exits outright.
 *
 * Setting a flag is the right FIRST response: the run loop notices it at the
 * top of the next step and the report still gets printed, which is most of
 * why a long run is worth interrupting at all.
 *
 * But the flag is only read between dispatch calls. Translated code that
 * loops without exhausting its cycle budget never returns, the loop never
 * gets to look at the flag, and the process cannot be stopped at all - not
 * by Ctrl-C and not by `timeout`, which sends one SIGTERM and then waits
 * forever for a process that caught it and carried on. That happened here:
 * a 30,000,000-step run sat at 21 minutes against a 20-minute timeout, in
 * state R, with the flag set and SIGTERM delivered.
 *
 * So the second signal is not polite. Anything that ignored the first one is
 * not going to answer the second either.
 */
static void on_interrupt(int sig)
{
    (void)sig;
    if (mgs_module_interrupted) {
        /* Say WHERE before going. A process that has to be killed is
         * exactly the one that cannot use any of the normal reporting
         * routes, and two runs ended with nothing printed at all - which
         * says only that it did not finish, not where it was. Written with
         * write(2) rather than printf because this runs in a signal
         * handler and stdio is not re-entrant. */
        char buf[64];
        uint32_t pc = mgs_module_last_pc;
        /* The GX marker is the finer-grained of the two: the run loop can
         * only say "in dispatch", which is true for the parser, the
         * rasteriser and the guest's own code alike. */
        const char* phase = (const char*)mgs_module_phase;
        const char* gxp = (const char*)mgs_gx_phase;
        unsigned i;
        memcpy(buf, "\n[wedged] last pc 0x00000000 in ", 32);
        for (i = 0; i < 8u; ++i) {
            unsigned nib = (pc >> ((7u - i) * 4u)) & 0xFu;
            buf[20 + i] = (char)(nib < 10u ? '0' + nib : 'A' + (nib - 10u));
        }
        (void)!write(2, buf, 32);
        if (phase) {
            size_t n = 0; while (phase[n] && n < 32u) ++n;
            (void)!write(2, phase, n);
        }
        (void)!write(2, " / gx ", 6);
        if (gxp) {
            size_t n = 0; while (gxp[n] && n < 32u) ++n;
            (void)!write(2, gxp, n);
        }
        (void)!write(2, "\n", 1);

        /* THE HOST CALL STACK, which is the thing that actually answers
         * "where is it".
         *
         * Every cheaper instrument tried before this one pointed somewhere
         * confident and wrong: a phase marker that reported the last event
         * rather than the current one, an abandon hook that released the
         * rasteriser without releasing the run, and a cycle budget that
         * changed nothing. backtrace() is not formally async-signal-safe,
         * but this path is about to _exit anyway, and a stack that is
         * occasionally garbled beats three rounds of inference. */
        {
            void* frames[24];
            int n = backtrace(frames, 24);
            (void)!write(2, "[wedged] host stack:\n", 21);
            backtrace_symbols_fd(frames, n, 2);
        }
        _exit(130);
    }
    mgs_module_interrupted = 1;
}

/* GXSetVtxDesc(attr, type) and __GXSetVCD(), traced together.
 *
 * The parser holds a vertex descriptor of POS + TEX0, which makes the
 * engine's sphere 20 bytes a vertex; the bytes the engine actually writes
 * repeat every 24. Both cannot be right, and the command stream shows no
 * descriptor write between the two draws - so the question is whether the
 * game asked for a different descriptor and the flush did not happen, or
 * whether it never asked. Tracing the setter and the flush separately is
 * what tells those apart. */
static void trace_setvtxdesc(void* cpu, const uint32_t* gpr)
{
    (void)cpu;
    fprintf(stderr, "[gx] GXSetVtxDesc(attr=%u, type=%u)\n", gpr[3], gpr[4]);
}

static void trace_setvcd(void* cpu, const uint32_t* gpr)
{
    (void)cpu; (void)gpr;
    fprintf(stderr, "[gx] __GXSetVCD() flushing\n");
}

/* GXBeginDisplayList(ptr, size).
 *
 * On hardware this REDIRECTS the write-gather pipe into a memory buffer:
 * the address the game stores to does not change, but the data is recorded
 * rather than executed. A host that routes every pipe write to the parser
 * executes the recording instead - with whatever vertex descriptor happens
 * to be live, rather than the one the list will be called under.
 *
 * That would explain a draw whose bytes measure 24 a vertex against a
 * descriptor that declares 20, which is where the boot currently loses the
 * stream. */
static void trace_begin_dl(void* cpu, const uint32_t* gpr)
{
    (void)cpu;
    fprintf(stderr, "[gx] GXBeginDisplayList(ptr=0x%08X, size=%u)\n",
            gpr[3], gpr[4]);
}

/* The two callbacks on the task the boot waits for: `init_cb` at +0x28 of
 * the DSPTaskInfo, which sets the flag the wait loop reads, and `done_cb` at
 * +0x30. Tracing them answers whether the handler is reaching the right task
 * at all, which nothing else here can. */
static void trace_dsp_initcb(void* cpu, const uint32_t* gpr)
{
    (void)cpu; (void)gpr;
    fprintf(stderr, "[dsp] init_cb ran\n");
}

static void trace_dsp_donecb(void* cpu, const uint32_t* gpr)
{
    (void)cpu; (void)gpr;
    fprintf(stderr, "[dsp] done_cb ran\n");
}

/* Who suspends and resumes threads.
 *
 * The boot ends with its prio-16 thread SUSPENDED and only the idle thread
 * running, so something suspended it and nothing resumed it. The argument is
 * the thread, and the link register says which caller - together that is the
 * whole question. */
static void trace_suspend(void* cpu, const uint32_t* gpr)
{
    fprintf(stderr, "[thr] OSSuspendThread(0x%08X) from 0x%08X\n",
            gpr[3], mgs_module_lr(cpu));
}

static void trace_resume(void* cpu, const uint32_t* gpr)
{
    fprintf(stderr, "[thr] OSResumeThread(0x%08X) from 0x%08X\n",
            gpr[3], mgs_module_lr(cpu));
}

/* Which queues are slept on, and which are woken.
 *
 * The boot goes quiet with its threads blocked on queues and only the idle
 * thread running. A queue that is slept on and never woken is the whole
 * answer, and the argument to both calls is the queue. */
static void trace_sleep(void* cpu, const uint32_t* gpr)
{
    (void)cpu;
    fprintf(stderr, "[thr] sleep on queue 0x%08X\n", gpr[3]);
}

static void trace_wakeup(void* cpu, const uint32_t* gpr)
{
    (void)cpu;
    fprintf(stderr, "[thr] wake  queue 0x%08X\n", gpr[3]);
}

/* Message queues, which is how the engine's own threads block.
 *
 * The thread that stops making progress is not on any OSSleepThread queue,
 * so it is waiting on a message that never arrives. Both calls take the
 * queue as their first argument, so counting sends against receives per
 * queue says which one is starved. */
static void trace_recv(void* cpu, const uint32_t* gpr)
{
    fprintf(stderr, "[msg] recv 0x%08X from 0x%08X\n",
            gpr[3], mgs_module_lr(cpu));
}

static void trace_send(void* cpu, const uint32_t* gpr)
{
    /* The link register here is the poster's own return address, which is
     * the same for every event and says nothing. What identifies the event
     * is the poster's CALLER, and `fn_1_888` saves it at +0x14 of the frame
     * it just built - so one read of the guest stack turns "an event was
     * posted" into "this one was". */
    uint32_t caller = mgs_module_guest_read32(cpu, gpr[1] + 0x14u);
    /* And one frame further. The poster is reached through a parameterised
     * wrapper, so its caller is always the wrapper and says nothing; the
     * wrapper's own caller is the event's actual source. Both frames are
     * 0x10 bytes with the return address at +0x14, so the grandparent's is
     * at +0x24. */
    uint32_t origin = mgs_module_guest_read32(cpu, gpr[1] + 0x24u);
    fprintf(stderr, "[msg] send 0x%08X msg=0x%08X caller 0x%08X origin 0x%08X\n",
            gpr[3], gpr[4], caller, origin);
}

/* Did the engine's main loop ever start, and does it still run?
 *
 * `fn_1_88` is an endless loop - it clears a flag and runs the per-frame
 * task scheduler, for ever - so a thread that enters it never leaves. No
 * thread is in it now and the scheduler is absent from the profile, which
 * leaves two possibilities that look identical from outside: it was entered
 * and its thread was stopped, or it was never entered at all. */
static void trace_mainloop(void* cpu, const uint32_t* gpr)
{
    static unsigned n;
    (void)gpr;
    if (n++ < 8u)
        fprintf(stderr, "[eng] main loop entered (#%u), lr 0x%08X\n",
                n, mgs_module_lr(cpu));
}

static void trace_sched(void* cpu, const uint32_t* gpr)
{
    static unsigned n;
    (void)cpu; (void)gpr;
    if (++n <= 4u || (n % 1000u) == 0u)
        fprintf(stderr, "[eng] task scheduler run #%u\n", n);
}

/* Mutexes, which is the third way a thread can block.
 *
 * The engine's main-loop thread is waiting with a queue pointer that appears
 * in neither the sleep trace nor the message trace, so it is on a mutex's
 * queue. A lock with no matching unlock is a held mutex, and the caller says
 * who is holding it. */
static void trace_lock(void* cpu, const uint32_t* gpr)
{
    fprintf(stderr, "[mtx] lock   0x%08X from 0x%08X\n",
            gpr[3], mgs_module_lr(cpu));
}

static void trace_unlock(void* cpu, const uint32_t* gpr)
{
    fprintf(stderr, "[mtx] unlock 0x%08X from 0x%08X\n",
            gpr[3], mgs_module_lr(cpu));
}

/* The semaphore the engine's main loop is blocked on.
 *
 * A task dispatched by the per-frame scheduler calls OSWaitSemaphore and
 * never comes back, which stops the whole loop. Whether that semaphore is
 * ever signalled - and by whom - is the question, and both calls take it as
 * their first argument. */
static void trace_sem_wait(void* cpu, const uint32_t* gpr)
{
    fprintf(stderr, "[sem] wait   0x%08X count=%d from 0x%08X\n",
            gpr[3], (int)mgs_module_guest_read32(cpu, gpr[3]),
            mgs_module_lr(cpu));
}

static void trace_sem_signal(void* cpu, const uint32_t* gpr)
{
    fprintf(stderr, "[sem] signal 0x%08X count=%d from 0x%08X\n",
            gpr[3], (int)mgs_module_guest_read32(cpu, gpr[3]),
            mgs_module_lr(cpu));
}

/* The engine loop the boot stops in (HANDOFF F130), at REL .text 0xF1314.
 *
 * Its body advances by r21 and compares against r30:
 *     r19 += r21;  while (r19 < r30) ...
 * and r21 is `1 << (r26 - r24)`. On PowerPC a shift count of 32 or more
 * yields ZERO, and a zero stride here never terminates - so the values of
 * r19, r21 and r30 say directly whether this is an infinite loop or merely a
 * long one, which is the whole question.
 *
 * Printed once. An infinite loop offers the same answer every time, and a
 * trace that repeats it a million times buries it. */
static void trace_stuck_loop(void* cpu, const uint32_t* gpr)
{
    /* SAMPLED REPEATEDLY, because one sample cannot tell a stream being
     * decoded from a stream being restarted. Identical parameters every time
     * mean the same Huffman table is being rebuilt from the same input;
     * varying ones mean real progress through different blocks. That is the
     * difference between "the engine is stuck" and "the engine is working".*/
    static uint64_t seen;
    static uint32_t max_hn, max_sum, over;
    ++seen;

    /* THE OVERFLOW CHECK, EVALUATED ON EVERY ITERATION.
     *
     * huft_build returns Z_MEM_ERROR when `*hn + z > MANY` (1440), and that
     * return sets no message - so it is invisible except here. Sampling one
     * iteration in 200,000 cannot catch it, because *hn grows THROUGH a
     * single huft_build call: the check that matters is the last one, not a
     * random one. Two guest reads per iteration is cheap enough to do them
     * all and keep the maximum. */
    {
        static uint32_t prev_hn; static uint64_t calls;
        uint32_t hn = mgs_module_guest_read32(cpu, gpr[22]);
        uint32_t sum = hn + gpr[30];
        if (hn > max_hn) max_hn = hn;
        if (sum > max_sum) max_sum = sum;
        if (sum > 1440u) ++over;
        /* *hn only GROWS within one inflate_trees_dynamic call, which resets
         * it to 0 before the literal tree. So a decrease is a new call, and
         * counting decreases distinguishes "called for ever" from "one call
         * that never returns" - two faults with nothing in common. */
        if (hn < prev_hn) ++calls;
        prev_hn = hn;
        if (seen != 1u && (seen % 200000u) == 0u)
            fprintf(stderr, "[calls] %llu iterations, %llu restarts of the "
                            "tree build (*hn went backwards)\n",
                    (unsigned long long)seen, (unsigned long long)calls);
        /* huft_build's own loop variables, named as zlib names them.
         * k and g are bit lengths and cannot exceed BMAX (15); h indexes the
         * table stack and must not go negative; w is the bits already
         * consumed. Anything outside those ranges is the corruption, and
         * which one is outside says which loop is running away. */
        /* CTR AND r29 MUST FALL IN LOCKSTEP.
         *
         * The loop is `while (a--)`, compiled to a bdnz counted loop: mtctr
         * loads r29's value, then each turn decrements CTR and r29 together.
         * They can only disagree if something outside the loop changed one of
         * them - and the only thing that touches CTR from outside is an
         * interrupt saving and restoring the context. So printing both is the
         * whole diagnosis: equal means the loop is honest and the data is
         * wrong, unequal means we are corrupting the guest. */
        if (seen != 1u && (seen % 200000u) == 0u) {
            uint32_t ctr;
            memcpy(&ctr, (const uint8_t*)cpu + 648u, sizeof ctr);
            fprintf(stderr, "[ctr] CTR=%u (0x%08X)  r29=%d  %s\n",
                    ctr, ctr, (int32_t)gpr[29],
                    ctr == gpr[29] ? "in step"
                                   : "DESYNCHRONISED - CTR was clobbered");
        }
        if (seen != 1u && (seen % 200000u) == 0u)
            fprintf(stderr, "[huft] k=%d g=%d h=%d w=%d l=%d  i=0x%08X "
                            "a_left=%d%s\n",
                    (int32_t)gpr[26], (int32_t)gpr[28], (int32_t)gpr[27],
                    (int32_t)gpr[24], (int32_t)gpr[11], gpr[12],
                    (int32_t)gpr[29],
                    ((int32_t)gpr[26] > 15 || (int32_t)gpr[28] > 15 ||
                     (int32_t)gpr[27] < 0 || (int32_t)gpr[24] < 0)
                        ? "   <-- OUT OF RANGE" : "");
    }

    if (seen != 1u && (seen % 200000u) != 0u) return;
    fprintf(stderr, "[many] over %llu iterations: max *hn=%u  max *hn+z=%u  "
                    "MANY=1440  times over: %u\n",
            (unsigned long long)seen, max_hn, max_sum, over);

    /* THE z_stream, RECOVERED BY WALKING ONE FRAME UP.
     *
     * A hook on inflate's entry never fires - the call happens inside a
     * dispatch chunk (HANDOFF F131) - but this one, inside huft_build's hot
     * inner loop, fires constantly because the loop is where the pc lands.
     * So the stream is reached from here instead of being waited for:
     *
     *   huft_build's sp  ->  back chain  ->  inflate_trees_dynamic's frame,
     *   whose `stmw r24, 0x4a0(r1)` puts the saved r31 at +0x4BC, and r31 is
     *   the z_stream because that is where it stores z->msg (+0x18).
     *
     * total_in and total_out are zlib's own progress counters. If they do not
     * move between samples, inflate is not consuming or producing anything,
     * and no amount of Huffman detail matters. */
    {
        uint32_t huft_sp = gpr[1];
        uint32_t itd_sp  = mgs_module_guest_read32(cpu, huft_sp);
        uint32_t z       = mgs_module_guest_read32(cpu, itd_sp + 0x4BCu);
        static uint32_t last_ti, last_to;
        uint32_t ni, ai, ti, no, ao, to, msg;

        if (z < 0x80000000u || z >= 0x81800000u) {
            fprintf(stderr, "[zlib] sample %llu: frame walk gave z=0x%08X, "
                            "not a guest pointer - walk is wrong\n",
                    (unsigned long long)seen, z);
            return;
        }
        ni  = mgs_module_guest_read32(cpu, z + 0x00u);
        ai  = mgs_module_guest_read32(cpu, z + 0x04u);
        ti  = mgs_module_guest_read32(cpu, z + 0x08u);
        no  = mgs_module_guest_read32(cpu, z + 0x0Cu);
        ao  = mgs_module_guest_read32(cpu, z + 0x10u);
        to  = mgs_module_guest_read32(cpu, z + 0x14u);
        msg = mgs_module_guest_read32(cpu, z + 0x18u);
        fprintf(stderr,
                "[zlib] sample %llu  z=0x%08X  in: next 0x%08X avail %u "
                "total %u%s   out: next 0x%08X avail %u total %u%s  msg 0x%08X\n",
                (unsigned long long)seen, z, ni, ai, ti,
                (seen > 1u && ti == last_ti) ? " (STALLED)" : "",
                no, ao, to,
                (seen > 1u && to == last_to) ? " (STALLED)" : "", msg);
        last_ti = ti; last_to = to;

        /* AND THE TABLE-SPACE COUNTER, which is the silent failure.
         *
         * huft_build returns Z_MEM_ERROR when `*hn + z > MANY` - at
         * REL 0xF1208 that constant is 0x5A0, 1440, zlib's MANY exactly. That
         * return sets NO message (inflate_trees_dynamic passes -4 straight
         * out), which is why scanning for error strings found nothing while
         * the stream sat frozen.
         *
         * r22 holds the `hn` pointer and r30 the table size z, both still
         * live at this point in the inner loop, so the check can be evaluated
         * here without hooking the branch itself. */
        /* WHICH WORDS OF ZLIB'S STATE ACTUALLY MOVE.
         *
         * Everything else about this loop is identical every time round - the
         * table, its address, its parameters, the output buffer. So the loop
         * is driven by whatever DOES change, and the cheapest way to find it
         * is to photograph the region once and diff it later, inside a single
         * run. Two runs at different budgets would answer the same question
         * more slowly and with more that could differ for unrelated reasons.
         *
         * 64 KB around the z_stream covers it and its internal state, which
         * the allocator put next to it. */
        /* WHERE zlib's REAL STATE LIVES, before assuming the snapshot below
         * covers it. z->state is the inflate internal_state, and its
         * `blocks` member is the inflate_blocks state that holds the bit
         * buffer and the mode - the things that must move if the decoder is
         * advancing. If either lies outside the snapshotted window, "nothing
         * changed" says nothing about them. */
        {
            uint32_t st = mgs_module_guest_read32(cpu, z + 0x1Cu);
            fprintf(stderr, "[zstate] z->state=0x%08X", st);
            if (st >= 0x80000000u && st < 0x81800000u) {
                unsigned k;
                fprintf(stderr, "  words:");
                for (k = 0; k < 10u; ++k)
                    fprintf(stderr, " %u:0x%08X", k,
                            mgs_module_guest_read32(cpu, st + k * 4u));
            }
            fprintf(stderr, "\n");
        }

        {
            enum { SNAP_BASE = 0x81700000, SNAP_WORDS = 0x10000 / 4 };
            static uint32_t snap[SNAP_WORDS];
            static int taken;
            unsigned w, changed = 0u, shown = 0u;

            if (!taken) {
                taken = 1;
                for (w = 0; w < SNAP_WORDS; ++w)
                    snap[w] = mgs_module_guest_read32(cpu, SNAP_BASE + w * 4u);
            } else {
                fprintf(stderr, "[state] words changed since the first sample:\n");
                for (w = 0; w < SNAP_WORDS; ++w) {
                    uint32_t now = mgs_module_guest_read32(cpu, SNAP_BASE + w * 4u);
                    if (now == snap[w]) continue;
                    ++changed;
                    if (shown < 24u) {
                        uint32_t addr = SNAP_BASE + w * 4u;
                        fprintf(stderr, "   0x%08X  0x%08X -> 0x%08X%s\n",
                                addr, snap[w], now,
                                addr == z + 0x08u ? "   (z->total_in)" :
                                addr == z + 0x14u ? "   (z->total_out)" :
                                addr == z + 0x00u ? "   (z->next_in)" :
                                addr == z + 0x0Cu ? "   (z->next_out)" :
                                addr == z + 0x04u ? "   (z->avail_in)" :
                                addr == z + 0x10u ? "   (z->avail_out)" :
                                addr == z + 0x1Cu ? "   (z->state)" : "");
                        ++shown;
                    }
                }
                fprintf(stderr, "[state] %u of %u words changed in 64 KB\n",
                        changed, (unsigned)SNAP_WORDS);
            }
        }

        {
            uint32_t hn = mgs_module_guest_read32(cpu, gpr[22]);
            uint32_t zsz = gpr[30];
            fprintf(stderr,
                    "[huft] *hn=%u  z=%u  *hn+z=%u  MANY=1440  ->  %s\n",
                    hn, zsz, hn + zsz,
                    (hn + zsz) > 1440u ? "Z_MEM_ERROR (silent)" : "ok");
        }
        return;
    }
    fprintf(stderr,
            "[loop] at REL 0xF1314:  r19=0x%08X (index)  r21=0x%08X (stride)"
            "  r30=0x%08X (bound)  r25=0x%08X (base)\n"
            "[loop] r24=0x%08X  r26=0x%08X  r11=0x%08X  r12=0x%08X%s\n",
            gpr[19], gpr[21], gpr[30], gpr[25],
            gpr[24], gpr[26], gpr[11], gpr[12],
            gpr[21] == 0u ? "   <-- STRIDE IS ZERO: this loop cannot end"
                          : "");
}

/* huft_build's RETURN CODE, read where the caller tests it.
 *
 * 0x7F0F95CC is the `cmpwi r3, 0` immediately after the first call in
 * inflate_trees_dynamic, so r3 here is exactly what huft_build returned:
 * 0 on success, and -3, -4 or -5 for Z_DATA_ERROR, Z_MEM_ERROR or
 * Z_BUF_ERROR. That single number decides the whole diagnosis - a boot that
 * inflates successfully for ever is decompressing something enormous, and one
 * that fails for ever is being handed data that is wrong. */
static void trace_huft_result(void* cpu, const uint32_t* gpr)
{
    static uint64_t ok, data_err, mem_err, buf_err, other, total;
    int32_t r3 = (int32_t)gpr[3];
    (void)cpu;
    ++total;
    if (r3 == 0)       ++ok;
    else if (r3 == -3) ++data_err;
    else if (r3 == -4) ++mem_err;
    else if (r3 == -5) ++buf_err;
    else               ++other;
    if ((total % 20000u) == 0u)
        fprintf(stderr, "[huft] %llu returns:  ok %llu  Z_DATA_ERROR %llu  "
                        "Z_MEM_ERROR %llu  Z_BUF_ERROR %llu  other %llu\n",
                (unsigned long long)total, (unsigned long long)ok,
                (unsigned long long)data_err, (unsigned long long)mem_err,
                (unsigned long long)buf_err, (unsigned long long)other);
}

/* DID INFLATE FAIL? Answered from guest memory, not from a pc hook.
 *
 * zlib sets `z->msg` to one of five string constants when a Huffman tree is
 * malformed. Those constants live in the module's own data, so if any of them
 * is POINTED AT by a word anywhere in guest RAM, inflate reported that error
 * to somebody. That is a fact about memory and cannot be missed the way a pc
 * hook can - which matters here, because the hook on huft_build's return site
 * never fired at all (the call returns inside one dispatch chunk, the blind
 * spot recorded in HANDOFF F131).
 *
 * Finds the string by content first, because the module's load address is the
 * game's choice and has already changed once.
 */
static void find_inflate_error(const GuestMemory* mem)
{
    static const char* const msgs[] = {
        "oversubscribed literal/length tree",
        "incomplete literal/length tree",
        "oversubscribed distance tree",
        "incomplete distance tree",
        "empty distance tree with lengths",
    };
    uint32_t lo = 0x80000000u, hi = 0x81800000u;
    unsigned m;

    for (m = 0; m < sizeof msgs / sizeof msgs[0]; ++m) {
        size_t len = strlen(msgs[m]);
        uint32_t a, found = 0u;
        for (a = lo; a + (uint32_t)len < hi; ++a) {
            size_t i = 0;
            while (i < len && guest_read8(mem, a + (uint32_t)i) ==
                              (uint8_t)msgs[m][i]) ++i;
            if (i == len) { found = a; break; }
        }
        if (!found) { printf("  \"%s\": not in guest RAM\n", msgs[m]); continue; }
        {
            uint32_t p, refs = 0u, first = 0u;
            for (p = lo; p + 4u < hi; p += 4u)
                if (guest_read32(mem, p) == found) {
                    if (!refs) first = p;
                    ++refs;
                }
            printf("  \"%s\" at 0x%08X, pointed at by %u word%s%s",
                   msgs[m], found, refs, refs == 1u ? "" : "s",
                   refs ? "" : "\n");
            if (refs) printf(" (first 0x%08X)  <-- INFLATE REPORTED THIS\n", first);
        }
    }
}

/* GXInitTexObj's arguments, at the call.
 *
 * The tex obj at 0x7F50068C ends up holding an image base 55.6 MB into a 24 MB
 * machine, and the write watch shows GXInitTexObj storing it faithfully - so
 * the bad value arrives as its `image_ptr` argument and the fault is in the
 * caller. This is a cross-module call (engine -> DOL), which is the case pc
 * hooks catch reliably.
 *
 * Only the calls that produce a bad pointer are printed: the game initialises
 * many texture objects and the interesting one is rare. */
static void trace_init_texobj(void* cpu, const uint32_t* gpr)
{
    uint32_t obj = gpr[3], img = gpr[4];
    static unsigned shown;

    if (img >= 0x80000000u && img < 0x81800000u) return;   /* ordinary, skip */
    if (shown >= 8u) return;
    ++shown;
    fprintf(stderr,
            "[texobj] GXInitTexObj(obj=0x%08X, image=0x%08X, %ux%u, fmt=0x%X)"
            "  called from lr 0x%08X%s\n",
            obj, img, gpr[5], gpr[6], gpr[7], mgs_module_lr(cpu),
            img >= 0x81800000u ? "   <-- image pointer is past MEM1" : "");
}

static void usage(const char* argv0)
{
    fprintf(stderr,
        "usage: %s [--disc1 <path>] [--disc2 <path>] [--report <file>]\n"
        "\n"
        "  A disc is an .iso/.gcm image or a folder extracted from one.\n"
        "  With no --disc1, the usual places are tried: $MGS_DISC1, a\n"
        "  remembered path, discs/<id>/disc1, then beside the executable.\n"
        "\n"
        "  --module <path>  a recompiled game module. Without one the host\n"
        "                   exercises the runtime but runs no game code.\n",
        argv0);
}

int main(int argc, char** argv)
{
    const char* disc1_arg = NULL;
    const char* disc2_arg = NULL;
    const char* report_path = NULL;
    const char* module_path = NULL;
    int headless = 0;
    char path1[1024], path2[1024];
    MgsDiscSource src1, src2;
    MgsDisc disc1, disc2;
    MgsRuntime rt;
    MgsDvd dvd;
    MgsJobPool* jobs;
    FILE* report = stdout;
    int i;

    for (i = 1; i < argc; ++i) {
        if (!strcmp(argv[i], "--disc1") && i + 1 < argc)       disc1_arg = argv[++i];
        else if (!strcmp(argv[i], "--disc2") && i + 1 < argc)  disc2_arg = argv[++i];
        else if (!strcmp(argv[i], "--report") && i + 1 < argc) report_path = argv[++i];
        else if (!strcmp(argv[i], "--module") && i + 1 < argc) module_path = argv[++i];
        else if (!strcmp(argv[i], "--headless")) headless = 1;
        else { usage(argv[0]); return 2; }
    }

    memset(&rt, 0, sizeof rt);
    if (!guest_memory_init(&rt.mem)) {
        fprintf(stderr, "could not allocate guest memory\n");
        return 1;
    }
    printf("guest memory: %u MB MEM1, %u MB ARAM\n",
           GUEST_RAM_SIZE / (1024u*1024u), GUEST_ARAM_SIZE / (1024u*1024u));

    if (!headless && !mgs_video_init("MGS: Twin Snakes")) {
        fprintf(stderr, "no window (%s); continuing headless\n", "SDL video unavailable");
        headless = 1;
    }
    overlay_line("MEM1 %u MB   ARAM %u MB",
                 GUEST_RAM_SIZE/(1024u*1024u), GUEST_ARAM_SIZE/(1024u*1024u));

    src1 = mgs_disc_locate(1u, disc1_arg, NULL, "GGSPA4", path1, sizeof path1);
    if (src1 == MGS_DISC_SOURCE_NONE) {
        fprintf(stderr, "no disc 1 found. Pass --disc1 <path>.\n");
        guest_memory_free(&rt.mem);
        return 1;
    }
    if (!mgs_disc_mount(&disc1, path1)) {
        /* Naming what was tried AND where it came from, because "not found"
         * is a different problem from "found the wrong thing". */
        fprintf(stderr, "disc 1 did not mount: %s (from %s)\n",
                path1, mgs_disc_source_name(src1));
        guest_memory_free(&rt.mem);
        return 1;
    }
    printf("disc 1: %s  [%s, disc %u]  via %s\n",
           path1, disc1.game_id, disc1.disc_number + 1u,
           mgs_disc_source_name(src1));
    overlay_line("DISC 1: %s  (%u FST ENTRIES)", disc1.game_id, disc1.fst.entry_count);

    memset(&disc2, 0, sizeof disc2);
    src2 = mgs_disc_locate(2u, disc2_arg, NULL, "GGSPA4", path2, sizeof path2);
    if (src2 != MGS_DISC_SOURCE_NONE && mgs_disc_mount(&disc2, path2))
        printf("disc 2: %s  [%s, disc %u]\n", path2, disc2.game_id,
               disc2.disc_number + 1u);
    else
        printf("disc 2: not mounted (the swap will be refused until it is)\n");

    mgs_profile_start();
    jobs = mgs_jobs_create(0u);
    {
        /* The rasteriser is the one piece of runtime work heavy enough to be
         * worth splitting across cores, and the only one on the frame's
         * critical path. */
        void mgs_display_set_jobs(void* pool);
        void mgs_display_set_fps_cap(unsigned fps);
        mgs_display_set_jobs(jobs);
        /* A window gets paced to a frame rate; a headless run does not,
         * because sleeping on the host clock would make it unreproducible
         * and reproducibility is the whole point of the headless path. */
        mgs_display_set_fps_cap(headless ? 0u : 60u);
    }
    printf("worker pool: %u threads\n", mgs_jobs_worker_count(jobs));
    overlay_line("DISC 2: %s", disc2.mounted ? "MOUNTED" : "NOT MOUNTED");
    overlay_line("WORKERS: %u THREADS", mgs_jobs_worker_count(jobs));

    mgs_dvd_init(&dvd, &disc1, jobs, &rt.mem);
    mgs_disc_set_bind(&rt, &disc1, disc2.mounted ? &disc2 : NULL);
    mgs_dvd_bind(&rt, &dvd);

    if (report_path) {
        report = fopen(report_path, "w");
        if (!report) { fprintf(stderr, "cannot write %s\n", report_path); report = stdout; }
    }
    rt.report_sink = report_to_stdout;
    rt.report_user = report;

    mgs_runtime_set_current(&rt);
    mgs_sched_init(&rt);

    /* Prove the pieces are wired before anything tries to run: a read that
     * goes through the real disc, the real FST and the real worker pool,
     * landing in real guest memory.
     */
    {
        long n = mgs_dvd_read_sync(&dvd, "shared/mgso_pal.rel", 0x80100000u, 0u, 32u);
        printf("self-check: read %ld bytes of the overlay; first word 0x%08X "
               "(its module id)\n", n, guest_read32(&rt.mem, 0x80100000u));
        overlay_line("DVD SELF-CHECK: %ld BYTES OK", n);
    }

    /* Load the executable into guest memory, as the apploader would. Without
     * this the right code runs against zeroed memory, and the failure is
     * quiet: an unloaded .sdata reads as zeros and zero is a plausible value
     * for almost anything. */
    {
        MgsDolInfo dol;
        if (!mgs_dol_load_from_disc(&rt.mem, &disc1, &dol)) {
            fprintf(stderr, "could not load main.dol into guest memory\n");
        } else {
            /* The boot ROM's legacy, written AFTER the DOL is loaded - the
             * DOL's own bss clear would otherwise wipe it - and before the
             * entry point runs, because OSInit reads all of it. */
            mgs_boot_info_init(&rt.mem, &disc1);

            printf("loaded main.dol: %u sections, %u bytes, bss 0x%08X+%u, entry 0x%08X\n",
                   dol.section_count, dol.loaded_bytes, dol.bss_address,
                   dol.bss_size, dol.entry_point);
            overlay_line("DOL: %u SECTIONS  %u KB", dol.section_count,
                         dol.loaded_bytes / 1024u);
        }
    }

    /* FIND THE MODULE RATHER THAN DEMAND IT.
     *
     * Needing --module and a step budget on every launch is what a wrapper
     * script exists to paper over, and a port that cannot be double-clicked
     * is not really an executable. The usual places, in order: next to this
     * binary, then the build tree relative to the working directory. */
    if (!module_path) {
        static char found[1024];
        const char* candidates[] = {
            "build/phase1/module/gGGSPA4_recomp.so",
            "../phase1/module/gGGSPA4_recomp.so",
            "../../phase1/module/gGGSPA4_recomp.so",
            "gGGSPA4_recomp.so",
        };
        unsigned ci;
        for (ci = 0; ci < sizeof candidates / sizeof candidates[0]; ++ci) {
            FILE* f = fopen(candidates[ci], "rb");
            if (f) {
                fclose(f);
                snprintf(found, sizeof found, "%s", candidates[ci]);
                module_path = found;
                printf("module: found %s\n", module_path);
                break;
            }
        }
    }

    if (!module_path) {
        printf("\nNo module found and none given: the runtime is up, but\n"
               "there is no game code to run. Pass --module <path>.\n");
    } else {
        MgsModule mod;
        void* cpu;

        printf("\nloading module: %s\n", module_path);
        if (!mgs_module_load(&mod, module_path)) {
            fprintf(stderr, "  failed: %s\n", mod.error);
        } else {
            overlay_line("MODULE: %s  ENTRY 0x%08X", mod.game_id, mod.entry_point);
            overlay_line("CHUNKS: %u   REL MODULES: %u", mod.chunk_ranges, mod.rel_modules);
            printf("  game id      : %s\n", mod.game_id);
            printf("  entry point  : 0x%08X\n", mod.entry_point);
            printf("  cpu state    : %u bytes\n", mod.cpu_state_size);
            printf("  code ranges  : %u   chunks: %u   rel modules: %u\n",
                   mod.code_ranges, mod.chunk_ranges, mod.rel_modules);

            /* The module's game id must match the disc's, or we would run one
             * game's code against another's assets and fail somewhere
             * unrelated to the cause. */
            if (strncmp(mod.game_id, disc1.game_id, 6) != 0) {
                fprintf(stderr, "  REFUSED: module is for %s, disc is %s\n",
                        mod.game_id, disc1.game_id);
            } else {
                cpu = mgs_module_new_cpu_state(&mod, rt.mem.ram, GUEST_RAM_SIZE);
                if (!cpu) {
                    fprintf(stderr, "  REFUSED: unexpected CPU state size %u; the\n"
                                    "  layout this host was built against has moved.\n",
                            mod.cpu_state_size);
                } else {
                    /* Point the SDK shims at the module's registers, so a
                     * shim reads exactly what the translated code passed. */
                    mgs_cpu_bind_registers(mgs_module_gpr(cpu));
                    mgs_cpu_bind_msr(mgs_module_msr_ptr(cpu));
                    mgs_host_install_spr_handler(cpu);
                    mgs_host_set_vmem(rt.mem.vmem);
                    mgs_module_set_vmem(rt.mem.vmem);

                    /* Finished DVD reads have to be reported on the guest
                     * thread. Nothing else does it, and until this was here
                     * every loader thread waited forever on a read that had
                     * already completed. */
                    mgs_module_set_pump(dvd_pump, &dvd);

                    /* The display path: GX's copy out to the external
                     * framebuffer, and the video interface's scan-out of it. */
                    /* MGS_NO_DISPLAY=1 leaves the graphics side out
                     * entirely, so a boot failure can be attributed to it or
                     * cleared of it in one run. */
                    if (!getenv("MGS_NO_DISPLAY")) {
                        mgs_display_init(&rt.mem);
                        /* Audio RAM. ARInit probes it before anything else
                         * can use it, and the boot stops in __ARChecksize
                         * without it. */
                        mgs_mmio_attach_aram(mgs_host_mmio(), &rt.mem);
                    s_display_mem = &rt.mem;
                    s_display_windowed = !headless;
                    /* Armed before the run, because the frame worth keeping
                     * is one of the early ones and the decision has to be
                     * made as each copy happens. */
                    mgs_display_set_best_path(getenv("MGS_SAVE_BEST"));
                    {
                        const char* ww = getenv("MGS_WATCH_WRITE");
                        if (ww)
                            mgs_host_set_write_watch(
                                (uint32_t)strtoul(ww, NULL, 0));
                    }
                    mgs_module_set_display(display_pump);
                    mgs_module_set_frame(frame_pump);
                    mgs_module_set_progress(progress_counter);
                    }

                    /* Tell the interrupt layer where the guest's own
                     * dispatcher is. Taken from the symbol map rather than
                     * hard-coded: it is a PAL-specific address, and the map is
                     * the one place that knows which build this is. */
                    {
                        MgsSdkFn probe = mgs_patch_lookup(0x800201A4u);
                        (void)probe;
                        mgs_interrupt_set_dispatch(0x800201A4u);  /* __OSDispatchInterrupt */
                        /* OSLink(module, bss). The bss pointer is the base
                         * every engine global is an offset from, and nothing
                         * else reports it. */
                        mgs_module_watch(0x80020AD8u);
                        /* Zero the overlay's .bss the moment linking is
                         * done. Until then the relocation tables occupying
                         * that memory are still needed. */
                        mgs_module_on_linked(mgs_clear_overlay_bss);
                        /* MGS_TRACE_ALLOC logs every call to the game's
                         * tracking allocator, with the file and line it was
                         * called from - which is how an arena running out
                         * becomes a list rather than a guess. */
                        if (getenv("MGS_TRACE_SEM")) {
                            mgs_module_trace_calls(0x80022B54u, trace_sem_wait);
                            mgs_module_trace_calls2(0x80022BC4u, trace_sem_signal);
                        }
                        if (getenv("MGS_TRACE_MUTEX")) {
                            mgs_module_trace_calls(0x80021028u, trace_lock);
                            mgs_module_trace_calls2(0x80021104u, trace_unlock);
                        }
                        if (getenv("MGS_TRACE_ENGINE")) {
                            /* REL offsets 0x88 and 0xF394C, at the overlay's
                             * load address. */
                            mgs_module_trace_calls(0x7F008174u, trace_mainloop);
                            mgs_module_trace_calls2(0x7F0FBA38u, trace_sched);
                        }
                        if (getenv("MGS_TRACE_MSG")) {
                            mgs_module_trace_calls(0x80020C3Cu, trace_recv);
                            mgs_module_trace_calls2(0x80020B74u, trace_send);
                        }
                        if (getenv("MGS_TRACE_QUEUES")) {
                            mgs_module_trace_calls(0x80023E3Cu, trace_sleep);
                            mgs_module_trace_calls2(0x80023F28u, trace_wakeup);
                        }
                        if (getenv("MGS_TRACE_HUFT"))
                            mgs_module_trace_calls4(0x7F0F95CCu, trace_huft_result);
                        if (getenv("MGS_TRACE_TEXOBJ"))
                            mgs_module_trace_calls(0x800439E8u, trace_init_texobj);
                        if (getenv("MGS_TRACE_LOOP"))
                            mgs_module_trace_calls3(0x7F0F9400u, trace_stuck_loop);
                        if (getenv("MGS_TRACE_THREADS")) {
                            mgs_module_trace_calls3(0x80023CCCu, trace_suspend);
                            mgs_module_trace_calls4(0x80023A44u, trace_resume);
                        }
                        if (getenv("MGS_TRACE_DSPCB")) {
                            mgs_module_trace_calls(0x80032924u, trace_dsp_initcb);
                            mgs_module_trace_calls2(0x80032988u, trace_dsp_donecb);
                        }
                        if (getenv("MGS_TRACE_VTXDESC")) {
                            mgs_module_trace_calls(0x80040B4Cu, trace_setvtxdesc);
                            mgs_module_trace_calls2(0x80041040u, trace_setvcd);
                            mgs_module_trace_calls3(0x80045B84u, trace_begin_dl);
                        }
                        if (getenv("MGS_TRACE_ALLOC")) {
                            mgs_module_trace_calls(0x8004E7BCu, trace_alloc);
                            mgs_module_trace_calls2(0x8004E830u, trace_free);
                            mgs_module_trace_calls3(0x8001CC08u, trace_createheap);
                            mgs_module_trace_calls4(0x8001CD90u, trace_setarenalo);
                        }
                    }
                    if (mod.set_patch_hook) {
                        mod.set_patch_hook(mgs_host_patch_dispatch);
                        printf("  patch table  : installed\n");
                    } else {
                        printf("  patch table  : module has no hook; the\n"
                               "                 translated SDK will run instead\n");
                    }

                    overlay_line("PATCH TABLE: %s",
                                 mod.set_patch_hook ? "INSTALLED" : "NOT AVAILABLE");
                    overlay_line("RUNNING FROM 0x%08X", mod.entry_point);
                    if (!headless) { overlay_draw(1); mgs_video_present(); }
                    printf("\nrunning from 0x%08X ...\n\n", mod.entry_point);
                    {
                        /* MGS_STEPS overrides the ceiling. The default is a
                         * few seconds of guest time - enough to reach the
                         * first frames - and the ceiling exists to bound a
                         * hang, not to end a healthy run. */
                        /* HEADLESS stops early on purpose: it is a
                         * measurement and wants a bounded, repeatable run.
                         * A WINDOW is a person watching, and stopping after
                         * a minute reads as a hang on whatever was on screen
                         * - which is exactly how it was read. */
                        uint64_t limit = headless ? 40000000ull
                                                  : 40000000000ull;
                        {
                            const char* env = getenv("MGS_STEPS");
                            if (env) limit = strtoull(env, NULL, 0);
                        }
                        signal(SIGINT, on_interrupt);
                        signal(SIGTERM, on_interrupt);
                        MgsRunResult r = mgs_module_run(&mod, cpu, limit);
                        static const char* why[] = {
                            "no code for that address",
                            "guest is spinning",
                            "step limit",
                            "unhandled exception",
                            "interrupted"
                        };
                        printf("\nstopped after %llu steps: %s, pc = 0x%08X\n",
                               (unsigned long long)r.steps, why[r.stop], r.pc);
                        {
                            /* r13 and r2 are the small-data-area bases, set
                             * once by __init_registers. A wrong r13 makes
                             * every sda-relative read return whatever happens
                             * to be at the wrong address - usually zero,
                             * which is plausible and therefore silent. */
                            const uint32_t* g = mgs_module_gpr(cpu);
                            printf("  r1(sp)=0x%08X r2=0x%08X r13=0x%08X r3=0x%08X\n",
                                   g[1], g[2], g[13], g[3]);
                            printf("  r25=0x%08X r26=0x%08X r27=0x%08X r31=0x%08X\n",
                                   g[25], g[26], g[27], g[31]);
                        }
                        if (r.stop == MGS_STOP_SPINNING)
                            printf("  msr = 0x%08X  (EE %s)\n", r.msr,
                                   (r.msr & 0x8000u) ? "enabled" : "DISABLED");
                        if (r.exception) {
                            printf("  exception 0x%08X  cause 0x%08X  "
                                   "faulting instruction srr0 = 0x%08X  msr = 0x%08X\n",
                                   r.exception, r.program_cause, r.srr0, r.msr);
                            if (r.program_cause & 0x00080000u) printf("  -> illegal instruction\n");
                            if (r.program_cause & 0x00040000u) printf("  -> privileged instruction\n");
                            if (r.program_cause & 0x00100000u) printf("  -> floating point\n");
                            if (r.program_cause & 0x00020000u) printf("  -> trap\n");
                        }
                        overlay_line("STEPS: %llu", (unsigned long long)r.steps);
                        overlay_line("STOP: %s", why[r.stop]);
                        {
                            const MgsEfb* e = mgs_display_efb();
                            printf("display: %llu EFB copies (%llu with clear), "
                                   "%llu frames presented, XFB 0x%08X\n",
                                   (unsigned long long)e->copies,
                                   (unsigned long long)e->clears,
                                   (unsigned long long)mgs_display_frames(),
                                   mgs_mmio_xfb_address(mgs_host_mmio()));
                        }
                        {
                            const MgsGx* g = mgs_display_gx();
                            const MgsGxRaster* rs = mgs_display_raster();
                            printf("GX: %llu commands, %llu primitives, "
                                   "%llu vertices, %llu triangles (%llu desync)\n",
                                   (unsigned long long)g->commands,
                                   (unsigned long long)g->primitives,
                                   (unsigned long long)g->vertices,
                                   (unsigned long long)g->triangles,
                                   (unsigned long long)g->desyncs);
                            printf("raster: %llu submitted, %llu clipped, "
                                   "%llu drawn, %llu pixels, %llu of them lit "
                                   "(%llu textured, %llu alpha-killed)\n",
                                   (unsigned long long)rs->submitted,
                                   (unsigned long long)rs->clipped,
                                   (unsigned long long)rs->drawn,
                                   (unsigned long long)rs->pixels,
                                   (unsigned long long)rs->pixels_lit,
                                   (unsigned long long)rs->textured,
                                   (unsigned long long)rs->alpha_killed);
                            {
                                /* The alpha test kills nothing. Either the
                                 * game never arms it, or it arms it and every
                                 * fragment passes - and a register that reads
                                 * zero because nobody wrote it looks exactly
                                 * like one deliberately cleared. */
                                const MgsGxBp* b2 = &mgs_display_gx()->bp;
                                uint32_t ac = mgs_bp_get(b2, BP_ALPHA_COMPARE);
                                printf("  ALPHA_COMPARE 0x%06X (written %d)"
                                       "  ref0=%u op0=%u ref1=%u op1=%u logic=%u\n",
                                       ac, b2->written[BP_ALPHA_COMPARE],
                                       ac & 0xFFu, (ac >> 16) & 7u,
                                       (ac >> 8) & 0xFFu, (ac >> 19) & 7u,
                                       (ac >> 22) & 3u);
                            }
                            {
                                unsigned i;
                                printf("  CMODE0 values in use (%u distinct):\n",
                                       rs->cmode_n);
                                for (i = 0; i < rs->cmode_n; ++i) {
                                    uint32_t c = rs->cmode_key[i];
                                    printf("    0x%06X  en=%u colupd=%u alpupd=%u "
                                           "src=%u dst=%u sub=%u  x%llu\n",
                                           c, c & 1u, (c >> 3) & 1u, (c >> 4) & 1u,
                                           (c >> 8) & 7u, (c >> 5) & 7u,
                                           (c >> 11) & 1u,
                                           (unsigned long long)rs->cmode_hit[i]);
                                }
                            }
                            {
                                const MgsGx* g = mgs_display_gx();
                                unsigned k;
                                printf("  desyncs by reason:\n");
                                for (k = 0; k < g->why_n; ++k)
                                    printf("    %-44s x%llu\n", g->why_key[k],
                                           (unsigned long long)g->why_hit[k]);
                                printf("  display lists: %llu called, "
                                       "%llu with a size that is not a "
                                       "multiple of 32, %llu ending "
                                       "mid-command\n",
                                       (unsigned long long)g->dl_calls,
                                       (unsigned long long)g->dl_ragged,
                                       (unsigned long long)g->dl_truncated);
                            }
                            {
                                unsigned k; uint64_t tot = 0;
                                for (k = 0; k < 20u; ++k) tot += rs->area_px[k];
                                printf("  triangle sizes (bbox area, and the "
                                       "share of all pixels they cover):\n");
                                for (k = 0; k < 20u; ++k)
                                    if (rs->area_tris[k])
                                        printf("    <=%7u px  %10llu tris  "
                                               "%5.1f%% of pixels\n", 1u << k,
                                               (unsigned long long)rs->area_tris[k],
                                               tot ? 100.0 * (double)rs->area_px[k]
                                                     / (double)tot : 0.0);
                            }
                            printf("  blend: %llu pixels blended, "
                                   "%llu writes masked off entirely\n",
                                   (unsigned long long)rs->blended,
                                   (unsigned long long)rs->write_masked);
                            printf("  coverage: %llu pixels inside a triangle, "
                                   "%llu rejected by the depth test (%.1f%%)\n",
                                   (unsigned long long)rs->covered,
                                   (unsigned long long)rs->depth_failed,
                                   rs->covered ? 100.0 * (double)rs->depth_failed
                                                 / (double)rs->covered : 0.0);
                            {
                                /* The two registers that decide whether
                                 * anything is textured at all, and whether
                                 * they were ever written. A register that
                                 * reads zero because nobody wrote it and one
                                 * the game deliberately cleared look
                                 * identical in a value alone. */
                                const MgsGxBp* bp = &mgs_display_gx()->bp;
                                printf("genMode 0x%06X (written %d)  "
                                       "TEV_ORDER0 0x%06X (written %d)\n",
                                       mgs_bp_get(bp, BP_GEN_MODE),
                                       mgs_bp_is_set(bp, BP_GEN_MODE),
                                       mgs_bp_get(bp, BP_TEV_ORDER),
                                       mgs_bp_is_set(bp, BP_TEV_ORDER));
                            }
                            {
                                unsigned i;
                                printf("TEV stages per triangle:");
                                for (i = 0; i < 16u; ++i)
                                    if (rs->tev_stages[i])
                                        printf("  %u:%llu", i + 1u,
                                               (unsigned long long)rs->tev_stages[i]);
                                printf("\n  untextured at stage 0 but textured "
                                       "at a later stage: %llu\n",
                                       (unsigned long long)rs->tex_on_later_stage);
                                printf("  triangles asking for a texture: %llu"
                                       "  of those, bind failed: %llu\n",
                                       (unsigned long long)rs->tex_wanted,
                                       (unsigned long long)rs->tex_bind_failed);
                                {
                                    unsigned k;
                                    printf("  untextured draws, TEV_COLOR_ENV "
                                           "(a,b,c,d = input selectors):\n");
                                    for (k = 0; k < rs->cenv_n; ++k) {
                                        uint32_t e = rs->cenv_key[k];
                                        printf("    0x%06X  a=%u b=%u c=%u d=%u"
                                               "  bias=%u sub=%u dest=%u  x%llu\n",
                                               e,
                                               (e >> 12) & 0xFu, (e >> 8) & 0xFu,
                                               (e >> 4) & 0xFu, e & 0xFu,
                                               (e >> 16) & 3u, (e >> 18) & 1u,
                                               (e >> 22) & 3u,
                                               (unsigned long long)rs->cenv_hits[k]);
                                    }
                                    printf("  untextured draws, vertex colour:\n");
                                    for (k = 0; k < rs->rascol_n; ++k)
                                        printf("    0x%08X  x%llu\n",
                                               rs->rascol_key[k],
                                               (unsigned long long)rs->rascol_hits[k]);
                                }
                            }
                            if (getenv("MGS_TRACE_TEXIMG")) {
                                unsigned k;
                                printf("  texture base addresses written "
                                       "(TX_SETIMAGE3), %u distinct:\n",
                                       g->teximg_n);
                                for (k = 0; k < g->teximg_n; ++k)
                                    printf("    reg=0x%06X -> 0x%08X%s\n",
                                           g->teximg[k],
                                           0x80000000u | (g->teximg[k] << 5),
                                           (0x80000000u | (g->teximg[k] << 5))
                                               >= 0x81800000u
                                               ? "   <-- past MEM1" : "");
                            }
                            {
                                unsigned k;
                                printf("  black pixels drawn OVER lit ones: "
                                       "%llu\n   ",
                                       (unsigned long long)rs->black_over_lit);
                                for (k = 0; k < 20u; ++k)
                                    if (rs->black_over_lit_x[k])
                                        printf(" %u-%u:%llu", k * 32u,
                                               k * 32u + 31u,
                                               (unsigned long long)
                                                   rs->black_over_lit_x[k]);
                                printf("\n");
                            }
                            {
                                unsigned k;
                                printf("  ALL 2D triangles by right edge "
                                       "(32px buckets):\n   ");
                                for (k = 0; k < 20u; ++k)
                                    if (rs->all2d_maxx[k])
                                        printf(" %u-%u:%llu", k * 32u,
                                               k * 32u + 31u,
                                               (unsigned long long)rs->all2d_maxx[k]);
                                printf("\n  ...of which textured:\n   ");
                                for (k = 0; k < 20u; ++k)
                                    if (rs->tex2d_maxx[k])
                                        printf(" %u-%u:%llu", k * 32u,
                                               k * 32u + 31u,
                                               (unsigned long long)rs->tex2d_maxx[k]);
                                printf("\n");
                            }
                            {
                                unsigned k;
                                printf("  viewports in use, %u distinct "
                                       "(half-width, x-origin, triangles):\n",
                                       rs->vp_n);
                                for (k = 0; k < rs->vp_n; ++k) {
                                    float hw, ox;
                                    memcpy(&hw, &rs->vp_halfw[k], 4);
                                    memcpy(&ox, &rs->vp_ox[k], 4);
                                    printf("    half-width %8.2f  x-origin %8.2f"
                                           "  -> x range %7.1f .. %7.1f   x%llu\n",
                                           hw, ox, ox - 342.0f - hw,
                                           ox - 342.0f + hw,
                                           (unsigned long long)rs->vp_hits[k]);
                                }
                            }
                            if (getenv("MGS_TRACE_TEXUSE")) {
                                unsigned k;
                                {
                                    unsigned q;
                                    printf("  texcoord ranges vs SU_SSIZE, "
                                           "%u distinct:\n", rs->uv_n);
                                    for (q = 0; q < rs->uv_n; ++q)
                                        printf("    u %8.3f .. %8.3f   "
                                               "SU_SSIZE=%u (scale %u)\n",
                                               rs->uv_lo[q], rs->uv_hi[q],
                                               rs->uv_ss[q], rs->uv_ss[q] + 1u);
                                }
                                printf("  textures SAMPLED, %u distinct:\n",
                                       rs->texuse_n);
                                for (k = 0; k < rs->texuse_n; ++k)
                                    printf("    0x%08X  fmt=0x%X  %ux%u\n",
                                           rs->texuse_addr[k], rs->texuse_fmt[k],
                                           rs->texuse_w[k], rs->texuse_h[k]);
                            }
                            printf("  texture refusals: %llu size, %llu texels, "
                                   "%llu palette, %llu alloc, %llu decode\n",
                                   (unsigned long long)rs->tex.refused_size,
                                   (unsigned long long)rs->tex.refused_texels,
                                   (unsigned long long)rs->tex.refused_palette,
                                   (unsigned long long)rs->tex.refused_alloc,
                                   (unsigned long long)rs->tex.refused_decode);
                            {
                                unsigned k;
                                printf("textures decoded by shape "
                                       "(a shape re-decoded often is dynamic - "
                                       "during the movie, the video frame):\n");
                                for (k = 0; k < rs->tex.shape_n; ++k)
                                    printf("    fmt 0x%X  %ux%u   x%llu\n",
                                           rs->tex.shape_key[k] >> 24,
                                           (rs->tex.shape_key[k] >> 12) & 0xFFFu,
                                           rs->tex.shape_key[k] & 0xFFFu,
                                           (unsigned long long)rs->tex.shape_hit[k]);
                            }
                            {
                                unsigned k;
                                printf("texture roughness by shape "
                                       "(a few = artwork, tens = noise):\n");
                                for (k = 0; k < rs->tex.rough_n; ++k)
                                    printf("    fmt 0x%X %ux%u  mean %llu  max %u\n",
                                           rs->tex.rough_key[k] >> 24,
                                           (rs->tex.rough_key[k] >> 12) & 0xFFFu,
                                           rs->tex.rough_key[k] & 0xFFFu,
                                           (unsigned long long)
                                             (rs->tex.rough_sum[k] /
                                              (rs->tex.rough_cnt[k] ? rs->tex.rough_cnt[k] : 1)),
                                           rs->tex.rough_max[k]);
                                    if (rs->tex.rough_addr[k])
                                        printf("        noisy at 0x%08X..0x%08X\n",
                                               rs->tex.rough_addr[k],
                                               rs->tex.rough_addr[k] +
                                                 rs->tex.rough_bytes[k]);
                            }
                            printf("textures: %llu decoded, %llu hits, "
                                   "%llu misses, %llu refused, %llu evicted\n",
                                   (unsigned long long)rs->tex.decodes,
                                   (unsigned long long)rs->tex.hits,
                                   (unsigned long long)rs->tex.misses,
                                   (unsigned long long)rs->tex.refused,
                                   (unsigned long long)rs->tex.evictions);
                        }
                        printf("GX draw-done: %llu offers, %llu delivered, "
                               "%llu acknowledged by the guest's handler\n",
                               (unsigned long long)mgs_interrupt_pe_seen(),
                               (unsigned long long)mgs_interrupt_pe_sent(),
                               (unsigned long long)mgs_host_mmio()->pe_finish_acks);
                        printf("DVD bytes delivered: %llu\n",
                               (unsigned long long)mgs_dvd_bytes());
                        printf("DVD reads completed: %llu  callbacks run: %llu"
                               "  deferred (guest had interrupts off): %llu\n",
                               (unsigned long long)mgs_dvd_completed(),
                               (unsigned long long)mgs_dvd_callbacks(),
                               (unsigned long long)mgs_dvd_deferred());
                        {
                            /* MGS_SAVE_FRAME=<path> writes the last frame the
                             * game presented, so a headless run can be looked
                             * at rather than only counted. */
                            {
                                /* GUEST MEMORY, HASHED. The last word on
                                 * progress: total_in and total_out only move
                                 * when inflate exits, so frozen counters are
                                 * also what "inside one long call" looks
                                 * like. Memory cannot hide that - a decoder
                                 * making progress writes SOMETHING. If two
                                 * runs with different step budgets hash the
                                 * same, the guest did nothing at all with the
                                 * extra steps. */
                                uint64_t h = 1469598103934665603ull;
                                uint32_t a, mb;
                                for (a = 0x80000000u; a < 0x81800000u; a += 4u) {
                                    h ^= guest_read32(&rt.mem, a);
                                    h *= 1099511628211ull;
                                }
                                printf("MEM1 hash: 0x%016llX\n",
                                       (unsigned long long)h);
                                /* PER-MEGABYTE, so two runs can be compared
                                 * and the change LOCALISED. A whole-memory
                                 * hash says only that something moved; which
                                 * megabyte moved says whether a decoder is
                                 * filling its output buffer or a loop is
                                 * churning its own scratch. */
                                printf("MEM1 by MB:");
                                for (mb = 0; mb < 24u; ++mb) {
                                    uint64_t g = 1469598103934665603ull;
                                    uint32_t b, base = 0x80000000u + mb * 0x100000u;
                                    for (b = 0; b < 0x100000u; b += 4u) {
                                        g ^= guest_read32(&rt.mem, base + b);
                                        g *= 1099511628211ull;
                                    }
                                    printf(" %02u:%04X", mb,
                                           (unsigned)((g >> 48) & 0xFFFFu));
                                }
                                printf("\n");
                            }
                            {
                                /* MGS_FIND_WORD=<hex>: where does a value LIVE?
                                 *
                                 * The font's texture address is written by the
                                 * game and points above its own arena, so the
                                 * fault is upstream of GX. Finding every place
                                 * that value is stored is the cheapest way to
                                 * reach whatever computed it: a texture object,
                                 * an asset header, or the allocation it should
                                 * have come from. */
                                const char* fw = getenv("MGS_FIND_WORD");
                                if (fw) {
                                    uint32_t want = (uint32_t)strtoul(fw, NULL, 0);
                                    /* MGS_FIND_MASK: match only some bits.
                                     * GXInitTexObj stores the image base
                                     * PACKED WITH its BP register byte, so a
                                     * raw search for the address cannot find
                                     * it and a masked one can. */
                                    const char* fm = getenv("MGS_FIND_MASK");
                                    uint32_t mask = fm ? (uint32_t)strtoul(fm, NULL, 0)
                                                       : 0xFFFFFFFFu;
                                    uint32_t hits = 0u;
                                    unsigned w;
                                    /* BOTH WINDOWS. The engine lives in the
                                     * second one at 0x7E000000, so a search
                                     * of MEM1 alone can only find what the
                                     * SDK put there - and the value in
                                     * question is written by the engine. */
                                    struct { uint32_t lo, hi; const char* n; } win[2] = {
                                        { 0x80000000u, 0x81800000u, "MEM1"   },
                                        { GUEST_VMEM_BASE,
                                          GUEST_VMEM_BASE + GUEST_VMEM_SIZE, "overlay" },
                                    };
                                    printf("searching for 0x%08X:\n", want);
                                    for (w = 0; w < 2u; ++w) {
                                        uint32_t a;
                                        for (a = win[w].lo; a + 4u < win[w].hi; a += 4u)
                                            if ((guest_read32(&rt.mem, a) & mask) == want) {
                                                if (hits < 24u)
                                                    printf("   %-7s 0x%08X = 0x%08X  "
                                                           "prev 0x%08X  next 0x%08X\n",
                                                           win[w].n, a,
                                                           guest_read32(&rt.mem, a),
                                                           guest_read32(&rt.mem, a - 4u),
                                                           guest_read32(&rt.mem, a + 4u));
                                                ++hits;
                                            }
                                    }
                                    printf("   %u occurrence%s\n",
                                           hits, hits == 1u ? "" : "s");
                                }
                            }
                            {
                                /* MGS_DUMP_TEXCOLS=<addr>,<w>,<h>: which
                                 * COLUMNS of a C8 texture hold anything.
                                 *
                                 * The text strips are 160x14 and 72x14 in
                                 * format 0x9, C8 - one byte per texel in 8x4
                                 * tiles. A column that is entirely zero holds
                                 * no glyph, so this says directly whether the
                                 * texture itself is short or the quad that
                                 * samples it is. No palette needed: a nonzero
                                 * INDEX is content whatever colour it maps
                                 * to. */
                                const char* dt = getenv("MGS_DUMP_TEXCOLS");
                                if (dt) {
                                    uint32_t base = 0; unsigned tw = 0, th = 0;
                                    if (sscanf(dt, "%x,%u,%u", &base, &tw, &th) == 3
                                        && tw && th && tw <= 1024u && th <= 1024u) {
                                        unsigned cx, cy, tiles_x = (tw + 7u) / 8u;
                                        printf("texture 0x%08X %ux%u (C8) column "
                                               "occupancy:\n   ", base, tw, th);
                                        for (cx = 0; cx < tw; ++cx) {
                                            unsigned any = 0;
                                            for (cy = 0; cy < th; ++cy) {
                                                unsigned tx = cx / 8u, ty = cy / 4u;
                                                unsigned ix = cx % 8u, iy = cy % 4u;
                                                uint32_t off = (ty * tiles_x + tx) * 32u
                                                             + iy * 8u + ix;
                                                if (guest_read8(&rt.mem, base + off))
                                                { any = 1; break; }
                                            }
                                            putchar(any ? '#' : '.');
                                            if ((cx % 80u) == 79u) printf("\n   ");
                                        }
                                        printf("\n");
                                        /* And the glyphs themselves, as ASCII.
                                         * Column occupancy says WHERE content
                                         * is; it cannot say WHAT it is, and
                                         * the question now is whether this
                                         * strip holds the whole sentence or a
                                         * short one. Index value as ink is
                                         * enough to read words. */
                                        {
                                            unsigned rx, ry, tiles = (tw + 7u) / 8u;
                                            printf("texture content:\n");
                                            for (ry = 0; ry < th; ++ry) {
                                                printf("   ");
                                                for (rx = 0; rx < tw; ++rx) {
                                                    unsigned tx = rx / 8u, ty = ry / 4u;
                                                    unsigned ix = rx % 8u, iy = ry % 4u;
                                                    uint32_t off = (ty * tiles + tx) * 32u
                                                                 + iy * 8u + ix;
                                                    uint8_t px2 = guest_read8(&rt.mem,
                                                                              base + off);
                                                    putchar(px2 ? (px2 > 0x7F ? '#' : '+')
                                                                : '.');
                                                }
                                                printf("\n");
                                            }
                                        }
                                    }
                                }
                            }
                            if (getenv("MGS_FIND_ZMSG")) {
                                printf("inflate error messages in guest RAM:\n");
                                find_inflate_error(&rt.mem);
                            }
                            const char* best = getenv("MGS_SAVE_BEST");
                            const char* out = getenv("MGS_SAVE_FRAME");
                            if (best)
                                printf("best frame: %u lit pixels of %ux%u "
                                       "(%.1f%%), written to %s\n",
                                       mgs_display_best_lit(),
                                       mgs_display_best_w(),
                                       mgs_display_best_h(),
                                       mgs_display_best_w() * mgs_display_best_h()
                                         ? 100.0 * mgs_display_best_lit() /
                                           (double)(mgs_display_best_w() *
                                                    mgs_display_best_h())
                                         : 0.0,
                                       best);
                            if (out && mgs_display_save_ppm(out, &rt.mem))
                                printf("wrote %s\n", out);
                        }
                        /* Where the guest actually spent its time. Printed
                         * last because it is the longest, and only when
                         * asked for. */
                        mgs_module_profile_dump(stdout, 30u);
                        {
                            /* MGS_DUMP_ADDR=0x... prints guest words at exit.
                             *
                             * A trace hook on a function compares the pc the
                             * RUN LOOP sees, and a three-instruction function
                             * reached by a goto inside one dispatch call is
                             * never sampled - so "the callback did not run"
                             * and "the hook cannot see it" look identical.
                             * Reading what the callback would have written
                             * tells them apart. */
                            const char* d = getenv("MGS_DUMP_ADDR");
                            while (d && *d) {
                                uint32_t a = (uint32_t)strtoul(d, (char**)&d, 0);
                                printf("guest 0x%08X = 0x%08X\n",
                                       a, guest_read32(&rt.mem, a));
                                while (*d == ',' || *d == ' ') ++d;
                            }
                        }
                        printf("ARAM: %llu transfers in, %llu out; "
                               "interrupts %llu delivered, %llu refused\n",
                               (unsigned long long)mgs_host_mmio()->aram.writes,
                               (unsigned long long)mgs_host_mmio()->aram.reads,
                               (unsigned long long)mgs_interrupt_aram_raised(),
                               (unsigned long long)mgs_interrupt_aram_refused());
                        printf("lazy FP context switches: %llu\n",
                               (unsigned long long)r.fp_switches);
                        printf("SDK calls served natively: %lu\n",
                               mgs_host_patched_calls());
                        {
                            struct MgsMmio* mm = mgs_host_mmio();
                            printf("mmio: %llu reads, %llu writes, %llu GX FIFO bytes\n",
                                   (unsigned long long)mm->reads,
                                   (unsigned long long)mm->writes,
                                   (unsigned long long)mm->wgpipe_bytes);
                            {   /* The pipe takes 1, 2, 4 and EIGHT byte
                                 * stores; eight is what psq_st and stfd do,
                                 * and is how immediate-mode vertices are
                                 * written. */
                                unsigned k;
                                printf("  write-gather stores by size:");
                                for (k = 1; k < 16u; ++k)
                                    if (mm->wgpipe_by_size[k])
                                        printf(" %u:%llu", k,
                                               (unsigned long long)
                                                   mm->wgpipe_by_size[k]);
                                printf("\n");
                            }
                            overlay_line("MMIO R:%llu W:%llu FIFO:%llu",
                                   (unsigned long long)mm->reads,
                                   (unsigned long long)mm->writes,
                                   (unsigned long long)mm->wgpipe_bytes);
                        }
                        {   /* What actually landed in the window. A range
                             * says bytes arrived; only the bytes say what. */
                            unsigned k, j;
                            const uint32_t at[] = { 0x7F000000u, 0x7F008000u };
                            for (j = 0; j < 2u; ++j) {
                                printf("  %08X:", at[j]);
                                for (k = 0; k < 8u; ++k)
                                    printf(" %08X",
                                           guest_read32(&rt.mem, at[j] + k * 4u));
                                printf("\n");
                            }
                        }
                        printf("second window: %llu reads, %llu writes, "
                               "0x%08X-0x%08X\n",
                               (unsigned long long)mgs_host_vmem_reads(),
                               (unsigned long long)mgs_host_vmem_writes(),
                               mgs_host_vmem_lo(), mgs_host_vmem_hi());
                        {   /* The engine's heaps, if the overlay linked -
                             * this is what the panic at memory.c:1197 is
                             * about. */
                            uint32_t mod_, bss_;
                            /* The overlay's load address is reported by
                             * OSLink, but that watch is a pc comparison and
                             * can be missed. It is also a constant of the
                             * build, so fall back on it rather than printing
                             * nothing: a dump that never appears taught me
                             * nothing for several runs. */
                            if (!mgs_module_watch_result(&mod_, &bss_)) {
                                mod_ = 0x7F008000u;
                                bss_ = 0u;
                                printf("OSLink not observed; assuming the "
                                       "overlay at 0x%08X\n", mod_);
                            }
                            {
                                printf("OSLink saw: module 0x%08X  bss 0x%08X\n",
                                       mod_, bss_);
                                /* The RECOMPILED overlay's globals, which is
                                 * where the engine actually keeps them. */
                                if (mod_) {
                                    uint32_t bss = mod_ + 0x4B6678u - 0x24AD8u;
                                    mgs_dump_heaps(cpu, bss);
                                    /* What the per-frame scheduler would
                                     * actually run. See host/heaps.c. */
                                    mgs_dump_tasks(cpu, bss);
                                }
                            }
                        }
                        mgs_dump_threads(cpu, NULL);
                        patch_report();
                        mgs_profile_report();
                        mgs_mmio_report_hot(mgs_host_mmio(), 6u);
                        printf("host instructions handled: %lu  (unhandled: %lu)\n",
                               mgs_host_spr_handled(), mgs_host_spr_unknown());
                        printf("system calls serviced: %llu\n",
                               (unsigned long long)r.syscalls);
                        printf("interrupts re-offered because they were still "
                               "asserted: %llu\n",
                               (unsigned long long)mgs_interrupt_redelivered());
                        printf("retrace ticks: %llu   interrupts delivered: %llu  "
                               "(refused while masked: %llu, handler failed: %llu)\n",
                               (unsigned long long)r.frames,
                               (unsigned long long)mgs_interrupt_delivered(),
                               (unsigned long long)mgs_interrupt_refused(),
                               (unsigned long long)mgs_interrupt_failed());
                        /* WHAT IS STILL PENDING WHEN WE STOP.
                         *
                         * A handler acknowledges its source by writing the
                         * cause bit back, so a bit still set here is an
                         * interrupt that was raised and never serviced. That
                         * distinguishes "we did not deliver it" from "we
                         * delivered it and the guest never ran the handler",
                         * which are opposite faults - and the tally alone
                         * cannot tell them apart, because a delivery counts
                         * as soon as the exception is taken. */
                        {
                            MgsMmio* mm = mgs_host_mmio();
                            uint32_t sr = mgs_mmio_read(mm, 0xCC003000u, 4);
                            uint32_t mr = mgs_mmio_read(mm, 0xCC003004u, 4);
                            uint32_t csr = mgs_mmio_read(mm, 0xCC00500Au, 2);
                            printf("PI cause 0x%08X  mask 0x%08X  "
                                   "still pending and armed: 0x%08X\n",
                                   sr, mr, sr & mr);
                            /* PI's DSP bit is shared by three sources, and
                             * the SDK's dispatcher reads THIS register to
                             * decide which. A PI bit set with no status bit
                             * here is an interrupt nobody can claim. */
                            {
                                /* THE MEMORY CARD BUS. EXI channel 0 and 1
                                 * status are the two hottest reads in a long
                                 * run - 2.65 million each - so what they
                                 * report is what the card layer is deciding
                                 * on. Bit 12 (0x1000) is EXT, "a device is
                                 * present"; bit 11 (0x800) is the insertion
                                 * interrupt. The SDK's __EXIProbe reads
                                 * exactly these. */
                                uint32_t c0 = mgs_mmio_read(mm, 0xCC006800u, 4);
                                uint32_t c1 = mgs_mmio_read(mm, 0xCC006814u, 4);
                                printf("EXI CSR  chan0 0x%08X (EXT %d, EXTINT %d)"
                                       "  chan1 0x%08X (EXT %d, EXTINT %d)\n",
                                       c0, (c0 >> 12) & 1, (c0 >> 11) & 1,
                                       c1, (c1 >> 12) & 1, (c1 >> 11) & 1);
                            }
                            printf("DSP control 0x%04X  status bits set: "
                                   "%s%s%s%s\n", csr,
                                   (csr & 0x08u) ? "AI " : "",
                                   (csr & 0x20u) ? "ARAM " : "",
                                   (csr & 0x80u) ? "DSP " : "",
                                   (csr & 0xA8u) ? "" : "(none)");
                        }
                        overlay_line("IRQ: %llu DELIVERED  %llu MASKED",
                               (unsigned long long)mgs_interrupt_delivered(),
                               (unsigned long long)mgs_interrupt_refused());
                        overlay_line("HOST INSNS: %lu  UNKNOWN: %lu",
                                     mgs_host_spr_handled(), mgs_host_spr_unknown());
                        overlay_line("STOPPED AT PC 0x%08X", r.pc);
                        overlay_line("SDK CALLS NATIVE: %lu", mgs_host_patched_calls());
                    }
                    mgs_cpu_unbind();
                    free(cpu);
                }
            }
            mgs_module_unload(&mod);
        }
    }

    /* Hold the window open so the result can be read.
     *
     * If the game ever reached the screen, the LAST FRAME IT DREW stays up:
     * that is the thing worth looking at, and replacing it with a wall of
     * counters at the moment the run ends means every screenshot of a working
     * renderer shows the boot overlay instead. The counters are on stdout
     * either way. Press a key to swap between them.
     */
    if (!headless && !s_quit_requested) {
        int showing_game = mgs_display_frames() > 0u;

        if (!showing_game) overlay_draw(0);
        else               mgs_display_present(mgs_host_mmio(), &rt.mem);

        printf("\n%s\n", showing_game
               ? "window: showing the last frame the game drew "
                 "(press SPACE for the boot report)"
               : "window: showing the boot report");

        while (mgs_video_present()) {
            SDL_Event ev;
            while (SDL_PollEvent(&ev)) {
                if (ev.type == SDL_EVENT_KEY_DOWN && ev.key.key == SDLK_SPACE) {
                    showing_game = !showing_game;
                    if (showing_game) mgs_display_present(mgs_host_mmio(), &rt.mem);
                    else              overlay_draw(0);
                }
            }
            SDL_Delay(16);
        }
        mgs_video_shutdown();
    }

    if (report != stdout) fclose(report);
    mgs_jobs_destroy(jobs);
    if (disc2.mounted) mgs_disc_unmount(&disc2);
    mgs_disc_unmount(&disc1);
    guest_memory_free(&rt.mem);
    return 0;
}
