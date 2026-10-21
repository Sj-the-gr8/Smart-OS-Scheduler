#ifndef _WIN32
#define _POSIX_C_SOURCE 200809L /* exposes nanosleep() under -std=c11 */
#endif

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include <stdatomic.h>
#include "mlq.h"
#include "filelock.h"

#ifdef _WIN32
#include <windows.h>
#else
#include <time.h>
#endif

/* Real milliseconds per simulated time unit (one page) in the live demo. */
#define TICK_MS 10

/* Real milliseconds per page that a submitter spends copying its document
 * into the shared spool buffer. The buffer is shared, so the copy happens
 * inside the critical section: while one thread copies, every other thread
 * that wants the mutex has to wait. This is what makes contention visible
 * when several jobs arrive at the same tick. Set to 0 to see how rarely
 * threads collide when the critical section is only a few instructions. */
#define SPOOL_COPY_MS_PER_PAGE 1

/* Real milliseconds a submitter spends rendering its document (converting
 * it to the printer's format) BEFORE taking the mutex - private work, so
 * no lock is needed. Threads leave pthread_cond_wait one at a time (each
 * must re-acquire the mutex on the way out); rendering lets the threads
 * woken by the same tick line up again, so they reach the mutex together. */
#define RENDER_MS 3

JobLevel job_level(const PrintJob *job) {
    if (strncmp(job->user_name, "Dean", 4) == 0) {
        return LEVEL_DEAN;
    }
    if (strncmp(job->user_name, "Faculty", 7) == 0 ||
        strncmp(job->user_name, "Staff", 5) == 0) {
        return LEVEL_FACULTY;
    }
    return LEVEL_STUDENT;
}

const char *level_name(JobLevel level) {
    switch (level) {
    case LEVEL_DEAN:    return "DEAN";
    case LEVEL_FACULTY: return "FACULTY";
    default:            return "STUDENT";
    }
}

/* ------------------------------------------------------------------ */
/* Scheduler state shared by the simulation and the live demo          */
/* ------------------------------------------------------------------ */

typedef struct {
    PrintQueue dean;
    PrintQueue faculty;
    PrintQueue student;
    PrintJob *running;        /* job that printed the last page; in no queue */
    int slice_used;           /* pages of the current RR quantum used by running */
    PrintJob *held_student;   /* partly printed Student job waiting to resume */
    int student_turn;         /* 1 while a Student job holds a starvation turn */
    int turn_used;            /* 1 after a starvation turn until FACULTY is served again */
    PrintJob *preempted;      /* job displaced by the last decision (for logging) */
    const char *reason;       /* why running was chosen (for logging) */
    int quantum;
    const SmartParams *params;
} MultiLevelQueue;

static void mlq_init(MultiLevelQueue *m, const SmartParams *params, int quantum) {
    memset(m, 0, sizeof(*m));
    init_queue(&m->dean);
    init_queue(&m->faculty);
    init_queue(&m->student);
    m->quantum = quantum < 1 ? 1 : quantum;
    m->params = params;
}

static void mlq_add(MultiLevelQueue *m, PrintJob *job) {
    job->state = JOB_STATE_READY;
    switch (job_level(job)) {
    case LEVEL_DEAN:    enqueue(&m->dean, job); break;
    case LEVEL_FACULTY: enqueue(&m->faculty, job); break;
    default:            enqueue(&m->student, job); break;
    }
}

/* Earliest-arriving urgent job in the Faculty queue (FCFS tie-break). */
static PrintJob *earliest_urgent(PrintQueue *q) {
    PrintJob *best = NULL;
    for (PrintJob *cur = q->head; cur != NULL; cur = cur->next) {
        if (cur->is_urgent &&
            (best == NULL || cur->arrival_time < best->arrival_time ||
             (cur->arrival_time == best->arrival_time && cur->job_id < best->job_id))) {
            best = cur;
        }
    }
    return best;
}

static int student_wait(const PrintJob *job, int t) {
    int printed = job->total_pages - job->remaining_pages;
    return t - job->arrival_time - printed;
}

static int student_starving(const MultiLevelQueue *m, int t) {
    int tau = m->params->tau_fair;
    if (m->held_student != NULL && student_wait(m->held_student, t) >= tau) {
        return 1;
    }
    for (PrintJob *cur = m->student.head; cur != NULL; cur = cur->next) {
        if (student_wait(cur, t) >= tau) {
            return 1;
        }
    }
    return 0;
}

/* A partly printed Student job is always finished before a new one starts;
 * otherwise Smart Queue picks (oldest starved job first, then best score). */
static PrintJob *take_student(MultiLevelQueue *m, int t) {
    if (m->held_student != NULL) {
        PrintJob *job = m->held_student;
        m->held_student = NULL;
        return job;
    }
    PrintJob *job = select_smart(&m->student, t, m->params);
    remove_job_by_id(&m->student, job->job_id);
    return job;
}

static void preempt_running(MultiLevelQueue *m) {
    PrintJob *job = m->running;
    job->state = JOB_STATE_READY;
    if (job_level(job) == LEVEL_FACULTY) {
        enqueue(&m->faculty, job);   /* RR: back to the tail */
    } else {
        m->held_student = job;       /* resumes before any other Student job */
    }
    m->preempted = job;
    m->running = NULL;
}

static PrintJob *start_job(MultiLevelQueue *m, PrintJob *job, int t, const char *reason) {
    m->running = job;
    m->slice_used = 0;
    m->reason = reason;
    job->state = JOB_STATE_RUNNING;
    if (job->start_time == -1) {
        job->start_time = t;
        job->response_time = t - job->arrival_time;
    }
    return job;
}

/* Called at every page boundary t: returns the job that prints the next
 * page, or NULL if every queue is empty. */
static PrintJob *mlq_next_page(MultiLevelQueue *m, int t) {
    PrintJob *cur = m->running;
    m->preempted = NULL;

    /* 1. DEAN: FCFS, run to completion, preempts everything else. */
    if (cur != NULL && job_level(cur) == LEVEL_DEAN) {
        return cur;
    }
    if (!is_queue_empty(&m->dean)) {
        if (cur != NULL) {
            preempt_running(m);
        }
        return start_job(m, dequeue(&m->dean), t, "Dean, FCFS");
    }

    /* 2. Keep the current job if its level's rule allows it. */
    if (cur != NULL) {
        if (job_level(cur) == LEVEL_FACULTY) {
            if (cur->is_urgent ||
                (m->slice_used < m->quantum && earliest_urgent(&m->faculty) == NULL)) {
                return cur;
            }
        } else if (m->student_turn || is_queue_empty(&m->faculty)) {
            return cur;
        }
        preempt_running(m);
    }

    /* 3. Pick a new job. */
    if (m->student_turn && m->held_student != NULL) {
        return start_job(m, take_student(m, t), t, "Student starvation turn (resumed)");
    }
    if (!is_queue_empty(&m->faculty)) {
        /* At most one starvation turn between two Faculty dispatches, so
         * the Student queue never outranks FACULTY for long. */
        if (!m->turn_used && student_starving(m, t)) {
            m->student_turn = 1;
            m->turn_used = 1;
            return start_job(m, take_student(m, t), t, "Student starvation turn");
        }
        m->turn_used = 0;
        PrintJob *urgent = earliest_urgent(&m->faculty);
        if (urgent != NULL) {
            remove_job_by_id(&m->faculty, urgent->job_id);
            return start_job(m, urgent, t, "Faculty URGENT, run to completion");
        }
        return start_job(m, dequeue(&m->faculty), t, "Faculty, Round Robin");
    }
    if (m->held_student != NULL || !is_queue_empty(&m->student)) {
        return start_job(m, take_student(m, t), t, "Student, SmartQueue");
    }
    return NULL;
}

/* Records that job printed one page, finishing at time t. Returns 1 if
 * the job is now complete. */
static int mlq_page_done(MultiLevelQueue *m, PrintJob *job, int t) {
    job->remaining_pages--;
    m->slice_used++;
    if (job->remaining_pages > 0) {
        return 0;
    }
    job->completion_time = t;
    job->turnaround_time = t - job->arrival_time;
    job->waiting_time = job->turnaround_time - job->total_pages;
    job->state = JOB_STATE_COMPLETED;
    if (job_level(job) == LEVEL_STUDENT) {
        m->student_turn = 0;
    }
    m->running = NULL;
    return 1;
}

int run_multilevel(PrintJob jobs[], int n, const SmartParams *params, int quantum) {
    PrintJob **order = build_arrival_order(jobs, n);
    MultiLevelQueue m;
    mlq_init(&m, params, quantum);

    int t = 0;
    int arrival_ptr = 0;
    int completed = 0;

    while (completed < n) {
        while (arrival_ptr < n && order[arrival_ptr]->arrival_time <= t) {
            mlq_add(&m, order[arrival_ptr++]);
        }
        PrintJob *job = mlq_next_page(&m, t);
        if (job == NULL) {
            t = order[arrival_ptr]->arrival_time;
            continue;
        }
        t++;
        if (mlq_page_done(&m, job, t)) {
            completed++;
        }
    }

    free(order);
    return t;
}

/* ------------------------------------------------------------------ */
/* Live concurrent demo                                                */
/* ------------------------------------------------------------------ */

static void sleep_ms(int ms) {
    if (ms <= 0) {
        return;
    }
#ifdef _WIN32
    Sleep((DWORD)ms);
#else
    struct timespec ts = { ms / 1000, (ms % 1000) * 1000000L };
    nanosleep(&ts, NULL);
#endif
}

static void sleep_one_tick(void) {
    sleep_ms(TICK_MS);
}

/* Everything the threads share. Every access to mlq, clock, submitted and
 * the statistics happens with lock held: that is the mutual-exclusion rule
 * that keeps the three queues consistent. The one exception is holder,
 * which is atomic so a blocked thread can read who is in its way. */
typedef struct {
    MultiLevelQueue mlq;
    pthread_mutex_t lock;       /* THE mutex guarding all fields of SharedSpool */
    pthread_cond_t tick;        /* printer -> submitters: clock advanced */
    pthread_cond_t job_ready;   /* submitters -> printer: a job was enqueued */
    _Atomic(const char *) holder;   /* name of the thread holding lock, or NULL */
    int clock;
    int submitted;

    /* Statistics for the contention summary. */
    long lock_acquisitions;
    long lock_contentions;      /* acquisitions that found the mutex held */
    long printer_decisions;     /* dispatches with jobs waiting in 2+ queues */
    long printer_wins[3];       /* ...won by DEAN / FACULTY / STUDENT */
} SharedSpool;

typedef struct {
    SharedSpool *spool;
    PrintJob **jobs;            /* this submitter's jobs, in arrival order */
    int count;
    const char *name;
    long blocked;               /* times this thread found the mutex held */
} Submitter;

/* Acquires the spool mutex on behalf of thread `who`. trylock is used only
 * to DETECT contention: if another thread is inside a critical section the
 * event is counted and printed, and the caller then blocks in
 * pthread_mutex_lock until the holder unlocks. */
static void spool_lock(SharedSpool *s, const char *who, long *blocked) {
    if (pthread_mutex_trylock(&s->lock) != 0) {
        const char *in_the_way = atomic_load(&s->holder);
        pthread_mutex_lock(&s->lock);           /* blocks here */
        (*blocked)++;
        s->lock_contentions++;
        printf("t=%-4d [%-17s] BLOCKED on mutex - held by %s; acquired after it unlocked\n",
               s->clock, who, in_the_way != NULL ? in_the_way : "another thread");
    }
    atomic_store(&s->holder, who);
    s->lock_acquisitions++;
}

static void spool_unlock(SharedSpool *s) {
    atomic_store(&s->holder, NULL);
    pthread_mutex_unlock(&s->lock);
}

/* pthread_cond_wait releases the mutex while asleep and re-acquires it
 * before returning, so the holder name is cleared and restored around it. */
static void spool_wait(SharedSpool *s, pthread_cond_t *cond, const char *who) {
    atomic_store(&s->holder, NULL);
    pthread_cond_wait(cond, &s->lock);
    atomic_store(&s->holder, who);
}

static void print_queue_sizes(const SharedSpool *s) {
    printf("| dean=%d faculty=%d student=%d\n",
           queue_size(&s->mlq.dean), queue_size(&s->mlq.faculty),
           queue_size(&s->mlq.student));
}

static void *submitter_thread(void *arg) {
    Submitter *sub = (Submitter *)arg;
    SharedSpool *s = sub->spool;

    for (int i = 0; i < sub->count; i++) {
        PrintJob *job = sub->jobs[i];

        /* 1. Sleep until the job is due (cond_wait releases the mutex). */
        spool_lock(s, sub->name, &sub->blocked);
        while (s->clock < job->arrival_time) {
            spool_wait(s, &s->tick, sub->name);
        }
        spool_unlock(s);

        /* 2. Render the document outside the lock (private work). Every
         *    submitter whose job is due at this tick woke on the same
         *    broadcast, so they finish rendering together and in step 3
         *    race for the mutex - the contention the BLOCKED lines show. */
        sleep_ms(RENDER_MS);

        /* 3. Copy the document into the shared spool buffer and enqueue it. */
        spool_lock(s, sub->name, &sub->blocked);
        /* ---- critical section: spool buffer, shared queues, counters ---- */
        sleep_ms(job->total_pages * SPOOL_COPY_MS_PER_PAGE);
        mlq_add(&s->mlq, job);
        s->submitted++;
        printf("t=%-4d [%-17s] enqueue job %-3d %-14s %3d pg%s -> %-7s ",
               s->clock, sub->name, job->job_id, job->user_name, job->total_pages,
               job->is_urgent ? " URGENT" : "       ", level_name(job_level(job)));
        print_queue_sizes(s);
        pthread_cond_signal(&s->job_ready);
        /* ---- end of critical section ---- */
        spool_unlock(s);
    }
    return NULL;
}

/* The printer is also the clock: at each tick it publishes the new time,
 * waits until every job due by then has been enqueued by the submitters,
 * then prints one page outside the lock. */
/* Jobs with work outstanding at each level, counting the job that printed
 * the last page and a partly printed Student job, not just the queues. */
static void waiting_per_level(const MultiLevelQueue *m, int counts[3]) {
    counts[LEVEL_DEAN] = queue_size(&m->dean);
    counts[LEVEL_FACULTY] = queue_size(&m->faculty);
    counts[LEVEL_STUDENT] = queue_size(&m->student) + (m->held_student != NULL);
    if (m->running != NULL) {
        counts[job_level(m->running)]++;
    }
}

static void printer_loop(SharedSpool *s, PrintJob **order, int n, const char *log_path,
                         long *blocked) {
    const char *me = "printer";
    int due = 0;
    int done = 0;
    PrintJob *last = NULL;

    spool_lock(s, me, blocked);
    /* ---- critical section: choose the next page ---- */
    for (int t = 0; done < n; t++) {
        s->clock = t;
        pthread_cond_broadcast(&s->tick);
        while (due < n && order[due]->arrival_time <= t) {
            due++;
        }
        while (s->submitted < due) {
            spool_wait(s, &s->job_ready, me);
        }

        /* Contention between the queues for the printer: snapshot how many
         * jobs each level has before the scheduler picks the next page. */
        int waiting[3];
        waiting_per_level(&s->mlq, waiting);
        int busy_levels = (waiting[0] > 0) + (waiting[1] > 0) + (waiting[2] > 0);

        PrintJob *job = mlq_next_page(&s->mlq, t);
        if (s->mlq.preempted != NULL && s->mlq.preempted != job) {
            printf("t=%-4d [%-17s] PREEMPT job %-3d (%d pg left)\n", t, me,
                   s->mlq.preempted->job_id, s->mlq.preempted->remaining_pages);
        }
        if (job != NULL && job != last) {
            if (busy_levels >= 2) {
                JobLevel winner = job_level(job);
                s->printer_decisions++;
                s->printer_wins[winner]++;
                printf("t=%-4d [%-17s] CONTENTION for printer: DEAN %d, FACULTY %d, STUDENT %d waiting -> %s wins\n",
                       t, me, waiting[LEVEL_DEAN], waiting[LEVEL_FACULTY],
                       waiting[LEVEL_STUDENT], level_name(winner));
            }
            printf("t=%-4d [%-17s] print   job %-3d (%s) ", t, me, job->job_id, s->mlq.reason);
            print_queue_sizes(s);
        }
        last = job;
        /* ---- end of critical section ---- */
        spool_unlock(s);

        sleep_one_tick();   /* the page prints while submitters keep enqueueing */

        spool_lock(s, me, blocked);
        /* ---- critical section: record the printed page ---- */
        if (job != NULL && mlq_page_done(&s->mlq, job, t + 1)) {
            done++;
            last = NULL;
            /* Release the mutex before taking the file lock: never hold both,
             * and never make submitters wait for disk I/O. */
            spool_unlock(s);
            int rc = filelock_append_job_log(log_path, job);
            spool_lock(s, me, blocked);
            if (rc == 0) {
                printf("t=%-4d [%-17s] done    job %-3d wait=%-3d -> %s (file lock)\n",
                       t + 1, me, job->job_id, job->waiting_time, log_path);
            } else {
                fprintf(stderr, "Warning: failed to log job_id=%d\n", job->job_id);
            }
        }
    }
    spool_unlock(s);
}

void demo_multilevel_concurrent(PrintJob jobs[], int n, const SmartParams *params,
                                int quantum, const char *log_path) {
    PrintJob **order = build_arrival_order(jobs, n);
    PrintJob **lists = malloc((size_t)(4 * n) * sizeof(PrintJob *));
    Submitter subs[4] = {
        { NULL, lists,         0, "dean-submitter", 0 },
        { NULL, lists + n,     0, "faculty-submitter", 0 },
        { NULL, lists + 2 * n, 0, "student-submit-1", 0 },
        { NULL, lists + 3 * n, 0, "student-submit-2", 0 },
    };

    /* Student jobs alternate between two submitters so that two threads
     * contend for the same STUDENT queue. */
    int next_student = 2;
    for (int i = 0; i < n; i++) {
        int k;
        switch (job_level(order[i])) {
        case LEVEL_DEAN:    k = 0; break;
        case LEVEL_FACULTY: k = 1; break;
        default:            k = next_student; next_student = 5 - next_student; break;
        }
        subs[k].jobs[subs[k].count++] = order[i];
    }

    SharedSpool spool;
    mlq_init(&spool.mlq, params, quantum);
    pthread_mutex_init(&spool.lock, NULL);
    pthread_cond_init(&spool.tick, NULL);
    pthread_cond_init(&spool.job_ready, NULL);
    spool.clock = -1;
    spool.submitted = 0;
    atomic_init(&spool.holder, NULL);
    spool.lock_acquisitions = 0;
    spool.lock_contentions = 0;
    spool.printer_decisions = 0;
    spool.printer_wins[0] = spool.printer_wins[1] = spool.printer_wins[2] = 0;

    printf("DEAN=%d (FCFS)  FACULTY=%d (Round Robin q=%d, urgent run to completion)  STUDENT=%d (SmartQueue)\n",
           subs[0].count, subs[1].count, spool.mlq.quantum, subs[2].count + subs[3].count);
    printf("Threads: 4 submitters + 1 printer sharing one mutex; 1 page = %d ms to print,\n"
           "%d ms to copy into the shared spool buffer (done while holding the mutex)\n",
           TICK_MS, SPOOL_COPY_MS_PER_PAGE);
    printf("Every line below is printed by a thread while it holds the mutex.\n\n");

    pthread_t tids[4];
    for (int i = 0; i < 4; i++) {
        subs[i].spool = &spool;
        pthread_create(&tids[i], NULL, submitter_thread, &subs[i]);
    }
    long printer_blocked = 0;
    printer_loop(&spool, order, n, log_path, &printer_blocked);
    for (int i = 0; i < 4; i++) {
        pthread_join(tids[i], NULL);
    }

    printf("\n--- Contention summary ---\n");
    printf("Mutex: acquired %ld times; %ld of those found it already held and had to block.\n",
           spool.lock_acquisitions, spool.lock_contentions);
    printf("  %-18s %s\n", "Thread", "Times blocked on the mutex");
    printf("  %-18s %ld\n", "printer", printer_blocked);
    for (int i = 0; i < 4; i++) {
        printf("  %-18s %ld\n", subs[i].name, subs[i].blocked);
    }
    printf("Printer: %ld dispatch decisions had jobs waiting in 2+ queues. Won by:\n",
           spool.printer_decisions);
    printf("  DEAN %ld   FACULTY %ld   STUDENT %ld\n", spool.printer_wins[LEVEL_DEAN],
           spool.printer_wins[LEVEL_FACULTY], spool.printer_wins[LEVEL_STUDENT]);

    pthread_cond_destroy(&spool.job_ready);
    pthread_cond_destroy(&spool.tick);
    pthread_mutex_destroy(&spool.lock);
    free(lists);
    free(order);
}
