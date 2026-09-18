/* Worker pool.
 *
 * Tested for the properties the runtime actually relies on: that every
 * submitted job runs exactly once, that wait() means finished rather than
 * merely dequeued, and that a refused submission is reported so the caller
 * can run it inline.
 */
#include "platform/jobs.h"
#include <stdio.h>
#include <stdatomic.h>
#include <unistd.h>

static int failures;
#define CHECK(cond) do { \
    if (!(cond)) { printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); ++failures; } \
} while (0)

static atomic_int g_ran;
static atomic_int g_concurrent;
static atomic_int g_max_concurrent;

static void count_job(void* user)
{
    int now;
    (void)user;
    now = atomic_fetch_add(&g_concurrent, 1) + 1;
    /* Record the high-water mark, to show work really is going wide. */
    for (;;) {
        int prev = atomic_load(&g_max_concurrent);
        if (now <= prev || atomic_compare_exchange_weak(&g_max_concurrent, &prev, now))
            break;
    }
    usleep(1000);
    atomic_fetch_add(&g_ran, 1);
    atomic_fetch_sub(&g_concurrent, 1);
}

int main(void)
{
    MgsJobPool* pool;
    unsigned hw = mgs_jobs_hardware_threads();
    int i;

    printf("hardware threads: %u\n", hw);
    CHECK(hw >= 1u);

    pool = mgs_jobs_create(0u);
    CHECK(pool != NULL);
    if (!pool) return 1;

    /* Sized to leave one core for the guest thread, which cannot be
     * parallelised and is the critical path.
     */
    printf("workers: %u\n", mgs_jobs_worker_count(pool));
    CHECK(mgs_jobs_worker_count(pool) >= 1u);
    if (hw > 1u) CHECK(mgs_jobs_worker_count(pool) == hw - 1u);

    for (i = 0; i < 200; ++i)
        CHECK(mgs_jobs_submit(pool, count_job, NULL));

    mgs_jobs_wait(pool);

    /* wait() must mean finished, not dequeued: the guest is about to read
     * these results.
     */
    CHECK(atomic_load(&g_ran) == 200);
    CHECK(atomic_load(&g_concurrent) == 0);

    printf("peak concurrency: %d\n", atomic_load(&g_max_concurrent));
    if (mgs_jobs_worker_count(pool) > 1u)
        CHECK(atomic_load(&g_max_concurrent) > 1);

    /* Submitting nothing is refused rather than crashing, so callers can pass
     * through a null without special-casing.
     */
    CHECK(mgs_jobs_submit(pool, NULL, NULL) == 0);
    CHECK(mgs_jobs_submit(NULL, count_job, NULL) == 0);

    mgs_jobs_destroy(pool);
    printf(failures ? "%d failure(s)\n" : "all job pool checks passed\n", failures);
    return failures != 0;
}
