/* Sanchit Jindal */
#include <stdio.h>
#include <stdlib.h>
#include <pthread.h>
#include <sched.h>
#include "sync.h"
#include "queue.h"

/* Shared state: one print queue that every submitter thread appends to. */
static PrintQueue shared_queue;
static pthread_mutex_t queue_mutex = PTHREAD_MUTEX_INITIALIZER;

typedef struct {
    PrintJob *jobs;         /* this thread's jobs */
    int count;
    int use_mutex;
    long blocked;           /* times this thread found the mutex held */
} Submitter;

/* enqueue() split into its read and write halves, with sched_yield() in
 * between. The yield only makes the existing race window easier to hit on
 * a fast machine; the plain enqueue() has the same read-modify-write and
 * the same bug, just less often.
 *
 * The lost update, step by step, without a mutex:
 *   T1 reads tail = J7          T2 reads tail = J7
 *   T1 sets J7->next = J8
 *                               T2 sets J7->next = J9   (overwrites J8)
 *   J8 is no longer reachable from head: that print job is lost,
 *   and size = old_size + 1 written by both threads loses a count too. */
static void enqueue_with_race_window(PrintQueue *q, PrintJob *job) {
    job->next = NULL;
    PrintJob *old_tail = q->tail;   /* READ shared state */
    int old_size = q->size;

    sched_yield();                  /* another thread may run here */

    if (old_tail == NULL) {         /* WRITE based on what was read */
        q->head = job;
    } else {
        old_tail->next = job;
    }
    q->tail = job;
    q->size = old_size + 1;
}

static void *submitter_thread(void *arg) {
    Submitter *s = (Submitter *)arg;

    for (int i = 0; i < s->count; i++) {
        if (s->use_mutex) {
            /* trylock first only to COUNT contention; then block normally. */
            if (pthread_mutex_trylock(&queue_mutex) != 0) {
                s->blocked++;
                pthread_mutex_lock(&queue_mutex);
            }
        }

        /* ---- critical section: shared_queue head/tail/size ---- */
        enqueue_with_race_window(&shared_queue, &s->jobs[i]);
        /* ---- end of critical section ---- */

        if (s->use_mutex) {
            pthread_mutex_unlock(&queue_mutex);
        }
    }
    return NULL;
}

/* Counts the jobs actually linked into the queue (capped as a safeguard). */
static int count_reachable(const PrintQueue *q, int cap) {
    int n = 0;
    for (const PrintJob *cur = q->head; cur != NULL && n <= cap; cur = cur->next) {
        n++;
    }
    return n;
}

static void run_once(int n_threads, int per_thread, int use_mutex, PrintJob *jobs) {
    int total = n_threads * per_thread;
    pthread_t *tids = malloc((size_t)n_threads * sizeof(pthread_t));
    Submitter *subs = malloc((size_t)n_threads * sizeof(Submitter));

    init_queue(&shared_queue);
    for (int i = 0; i < total; i++) {
        jobs[i].job_id = i + 1;
        jobs[i].next = NULL;
    }

    for (int t = 0; t < n_threads; t++) {
        subs[t].jobs = jobs + (size_t)t * per_thread;
        subs[t].count = per_thread;
        subs[t].use_mutex = use_mutex;
        subs[t].blocked = 0;
        pthread_create(&tids[t], NULL, submitter_thread, &subs[t]);
    }
    long blocked = 0;
    for (int t = 0; t < n_threads; t++) {
        pthread_join(tids[t], NULL);
        blocked += subs[t].blocked;
    }

    int reachable = count_reachable(&shared_queue, total);
    printf("  %-14s %9d %11d %10d %6d   ", use_mutex ? "WITH mutex" : "WITHOUT mutex",
           total, queue_size(&shared_queue), reachable, total - reachable);
    if (use_mutex) {
        printf("%ld\n", blocked);
    } else {
        printf("%s\n", "-");
    }

    free(subs);
    free(tids);
}

void demo_mutex_race(int n_threads, int jobs_per_thread) {
    int total = n_threads * jobs_per_thread;
    PrintJob *jobs = calloc((size_t)total, sizeof(PrintJob));
    if (jobs == NULL) {
        fprintf(stderr, "Error: out of memory for mutex demo\n");
        return;
    }

    printf("%d submitter threads each enqueue %d jobs into ONE shared print queue (%d total).\n",
           n_threads, jobs_per_thread, total);
    printf("enqueue() reads tail/size, then writes tail/size: a read-modify-write that\n");
    printf("must not be interleaved. Same code, same threads - only the mutex differs.\n\n");
    printf("  %-14s %9s %11s %10s %6s   %s\n", "Run", "Expected", "queue.size",
           "Reachable", "Lost", "Times a thread blocked on the mutex");

    run_once(n_threads, jobs_per_thread, 0, jobs);
    run_once(n_threads, jobs_per_thread, 1, jobs);

    printf("\nWithout the mutex two threads read the same tail and both link after it,\n");
    printf("so one job is overwritten (lost update). With the mutex only one thread is\n");
    printf("inside enqueue() at a time; the others block until it unlocks, so no job is lost.\n");

    free(jobs);
}
