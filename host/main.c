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

static void frame_pump(void)
{
    if (!s_display_windowed) return;
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

    jobs = mgs_jobs_create(0u);
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

    if (!module_path) {
        printf("\nNo --module given: the runtime is up, but there is no game\n"
               "code to run. Pass --module <gGGSPA4_recomp.so> to boot.\n");
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
                        uint64_t limit = 40000000ull;
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
                        printf("DVD reads completed: %llu  callbacks run: %llu"
                               "  deferred (guest had interrupts off): %llu\n",
                               (unsigned long long)mgs_dvd_completed(),
                               (unsigned long long)mgs_dvd_callbacks(),
                               (unsigned long long)mgs_dvd_deferred());
                        {
                            /* MGS_SAVE_FRAME=<path> writes the last frame the
                             * game presented, so a headless run can be looked
                             * at rather than only counted. */
                            const char* out = getenv("MGS_SAVE_FRAME");
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
    if (!headless) {
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
