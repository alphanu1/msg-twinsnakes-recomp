#include "jobs.h"

#include <pthread.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define MGS_JOB_QUEUE_CAP 1024u

typedef struct { MgsJobFn fn; void* user; } MgsJob;

struct MgsJobPool {
    pthread_t*      workers;
    unsigned        worker_count;

    pthread_mutex_t lock;
    pthread_cond_t  work_ready;     /* a job was queued, or we are stopping */
    pthread_cond_t  all_done;       /* queue empty and nothing in flight */

    MgsJob   queue[MGS_JOB_QUEUE_CAP];
    unsigned head, tail, count;
    unsigned in_flight;
    int      stopping;
};

unsigned mgs_jobs_hardware_threads(void)
{
    long n = sysconf(_SC_NPROCESSORS_ONLN);
    return n > 0 ? (unsigned)n : 1u;
}

static void* worker_main(void* arg)
{
    MgsJobPool* p = (MgsJobPool*)arg;
    for (;;) {
        MgsJob job;

        pthread_mutex_lock(&p->lock);
        while (p->count == 0u && !p->stopping)
            pthread_cond_wait(&p->work_ready, &p->lock);
        if (p->count == 0u && p->stopping) {
            pthread_mutex_unlock(&p->lock);
            return NULL;
        }
        job = p->queue[p->head];
        p->head = (p->head + 1u) % MGS_JOB_QUEUE_CAP;
        p->count--;
        p->in_flight++;
        pthread_mutex_unlock(&p->lock);

        job.fn(job.user);

        pthread_mutex_lock(&p->lock);
        p->in_flight--;
        /* Signal only when the pool is genuinely idle, so a waiter cannot
         * wake early and conclude that results are ready while a job is
         * still running.
         */
        if (p->count == 0u && p->in_flight == 0u)
            pthread_cond_broadcast(&p->all_done);
        pthread_mutex_unlock(&p->lock);
    }
}

MgsJobPool* mgs_jobs_create(unsigned workers)
{
    MgsJobPool* p = (MgsJobPool*)calloc(1, sizeof(MgsJobPool));
    unsigned i;
    if (!p) return NULL;

    if (workers == 0u) {
        unsigned hw = mgs_jobs_hardware_threads();
        /* Leave one core for the guest thread and the OS. The guest thread is
         * the critical path and cannot be parallelised, so starving it to run
         * background work is a net loss however many cores are spare.
         */
        workers = hw > 1u ? hw - 1u : 1u;
    }

    pthread_mutex_init(&p->lock, NULL);
    pthread_cond_init(&p->work_ready, NULL);
    pthread_cond_init(&p->all_done, NULL);

    p->workers = (pthread_t*)calloc(workers, sizeof(pthread_t));
    if (!p->workers) { free(p); return NULL; }

    for (i = 0; i < workers; ++i) {
        if (pthread_create(&p->workers[i], NULL, worker_main, p) != 0)
            break;                       /* fewer workers is still correct */
    }
    p->worker_count = i;

    if (p->worker_count == 0u) {         /* no threads: caller runs inline */
        mgs_jobs_destroy(p);
        return NULL;
    }
    return p;
}

void mgs_jobs_destroy(MgsJobPool* p)
{
    unsigned i;
    if (!p) return;

    pthread_mutex_lock(&p->lock);
    p->stopping = 1;
    pthread_cond_broadcast(&p->work_ready);
    pthread_mutex_unlock(&p->lock);

    for (i = 0; i < p->worker_count; ++i)
        pthread_join(p->workers[i], NULL);

    pthread_mutex_destroy(&p->lock);
    pthread_cond_destroy(&p->work_ready);
    pthread_cond_destroy(&p->all_done);
    free(p->workers);
    free(p);
}

int mgs_jobs_submit(MgsJobPool* p, MgsJobFn fn, void* user)
{
    if (!p || !fn) return 0;

    pthread_mutex_lock(&p->lock);
    if (p->stopping || p->count == MGS_JOB_QUEUE_CAP) {
        pthread_mutex_unlock(&p->lock);
        return 0;                        /* caller runs it inline instead */
    }
    p->queue[p->tail].fn = fn;
    p->queue[p->tail].user = user;
    p->tail = (p->tail + 1u) % MGS_JOB_QUEUE_CAP;
    p->count++;
    pthread_cond_signal(&p->work_ready);
    pthread_mutex_unlock(&p->lock);
    return 1;
}

void mgs_jobs_wait(MgsJobPool* p)
{
    if (!p) return;
    pthread_mutex_lock(&p->lock);
    while (p->count != 0u || p->in_flight != 0u)
        pthread_cond_wait(&p->all_done, &p->lock);
    pthread_mutex_unlock(&p->lock);
}

unsigned mgs_jobs_worker_count(const MgsJobPool* p)
{
    return p ? p->worker_count : 0u;
}
