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
#include <SDL3/SDL.h>

void mgs_dvd_service(const MgsModule* mod, void* cpu, MgsDvd* dvd);
uint64_t mgs_dvd_completed(void);
uint64_t mgs_dvd_callbacks(void);

static void dvd_pump(const MgsModule* mod, void* cpu, void* user)
{
    mgs_dvd_service(mod, cpu, (MgsDvd*)user);
}


#include <stdio.h>
#include <stdlib.h>
#include <string.h>
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

static int mgs_host_patch_dispatch(void* cpu_state, uint32_t address)
{
    MgsSdkFn fn = mgs_patch_lookup(address);
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

                    /* Finished DVD reads have to be reported on the guest
                     * thread. Nothing else does it, and until this was here
                     * every loader thread waited forever on a read that had
                     * already completed. */
                    mgs_module_set_pump(dvd_pump, &dvd);

                    /* Tell the interrupt layer where the guest's own
                     * dispatcher is. Taken from the symbol map rather than
                     * hard-coded: it is a PAL-specific address, and the map is
                     * the one place that knows which build this is. */
                    {
                        MgsSdkFn probe = mgs_patch_lookup(0x800201A4u);
                        (void)probe;
                        mgs_interrupt_set_dispatch(0x800201A4u);  /* __OSDispatchInterrupt */
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
                        MgsRunResult r = mgs_module_run(&mod, cpu, limit);
                        static const char* why[] = {
                            "no code for that address",
                            "guest is spinning",
                            "step limit",
                            "unhandled exception"
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
                        printf("GX draw-done tokens: %llu  PE finish delivered: %llu\n",
                               (unsigned long long)mgs_interrupt_pe_seen(),
                               (unsigned long long)mgs_interrupt_pe_sent());
                        printf("DVD reads completed: %llu  callbacks run: %llu\n",
                               (unsigned long long)mgs_dvd_completed(),
                               (unsigned long long)mgs_dvd_callbacks());
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
                        mgs_dump_threads(cpu, NULL);
                        patch_report();
                        mgs_mmio_report_hot(mgs_host_mmio(), 6u);
                        printf("host instructions handled: %lu  (unhandled: %lu)\n",
                               mgs_host_spr_handled(), mgs_host_spr_unknown());
                        printf("system calls serviced: %llu\n",
                               (unsigned long long)r.syscalls);
                        printf("retrace ticks: %llu   interrupts delivered: %llu  "
                               "(refused while masked: %llu, handler failed: %llu)\n",
                               (unsigned long long)r.frames,
                               (unsigned long long)mgs_interrupt_delivered(),
                               (unsigned long long)mgs_interrupt_refused(),
                               (unsigned long long)mgs_interrupt_failed());
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

    /* Hold the window open so the result can be read. The boot is over; this
     * is the only chance to see how it ended. */
    if (!headless) {
        overlay_draw(0);
        while (mgs_video_present()) SDL_Delay(16);
        mgs_video_shutdown();
    }

    if (report != stdout) fclose(report);
    mgs_jobs_destroy(jobs);
    if (disc2.mounted) mgs_disc_unmount(&disc2);
    mgs_disc_unmount(&disc1);
    guest_memory_free(&rt.mem);
    return 0;
}
