/* Ananyashree Sharma */
#include <stdlib.h>
#include "scheduler.h"

/* build_arrival_order() and admit_arrivals() are shared helpers in queue.c. */

typedef PrintJob *(*SelectFn)(PrintQueue *ready);

static PrintJob *select_fcfs(PrintQueue *ready) {
    return peek(ready);
}

static PrintJob *select_sjf(PrintQueue *ready) {
    PrintJob *best = NULL;
    for (PrintJob *cur = ready->head; cur != NULL; cur = cur->next) {
        if (best == NULL ||
            cur->total_pages < best->total_pages ||
            (cur->total_pages == best->total_pages && cur->arrival_time < best->arrival_time) ||
            (cur->total_pages == best->total_pages && cur->arrival_time == best->arrival_time &&
             cur->job_id < best->job_id)) {
            best = cur;
        }
    }
    return best;
}

PrintJob *select_priority(PrintQueue *ready) {
    PrintJob *best = NULL;
    for (PrintJob *cur = ready->head; cur != NULL; cur = cur->next) {
        if (best == NULL ||
            cur->priority > best->priority ||
            (cur->priority == best->priority && cur->arrival_time < best->arrival_time) ||
            (cur->priority == best->priority && cur->arrival_time == best->arrival_time &&
             cur->job_id < best->job_id)) {
            best = cur;
        }
    }
    return best;
}

/* Generic non-preemptive dispatch loop shared by FCFS, SJF and Priority.
 * For non-preemptive algorithms response_time == waiting_time, per the
 * Review-1 metric definitions. */
static int run_nonpreemptive(PrintJob jobs[], int n, SelectFn select) {
    PrintJob **order = build_arrival_order(jobs, n);
    PrintQueue ready;
    init_queue(&ready);

    int current_time = 0;
    int arrival_ptr = 0;
    int completed = 0;

    while (completed < n) {
        admit_arrivals(&ready, order, n, &arrival_ptr, current_time);

        if (is_queue_empty(&ready)) {
            current_time = order[arrival_ptr]->arrival_time;
            continue;
        }

        PrintJob *job = select(&ready);
        remove_job_by_id(&ready, job->job_id);

        job->state = JOB_STATE_RUNNING;
        job->start_time = current_time;
        job->response_time = job->start_time - job->arrival_time;
        job->waiting_time = job->response_time;

        current_time += job->total_pages;

        job->completion_time = current_time;
        job->turnaround_time = job->completion_time - job->arrival_time;
        job->state = JOB_STATE_COMPLETED;
        completed++;
    }

    free(order);
    return current_time;
}

int run_fcfs(PrintJob jobs[], int n) {
    return run_nonpreemptive(jobs, n, select_fcfs);
}

int run_sjf(PrintJob jobs[], int n) {
    return run_nonpreemptive(jobs, n, select_sjf);
}

int run_priority(PrintJob jobs[], int n) {
    return run_nonpreemptive(jobs, n, select_priority);
}

/* Page-quantum Round Robin: a discrete spooler-level abstraction of
 * preemptive time-slicing (see Section 8-D of the Review-1 report). */
int run_round_robin(PrintJob jobs[], int n, int quantum) {
    PrintJob **order = build_arrival_order(jobs, n);
    PrintQueue ready;
    init_queue(&ready);

    int current_time = 0;
    int arrival_ptr = 0;
    int completed = 0;

    admit_arrivals(&ready, order, n, &arrival_ptr, current_time);
    if (is_queue_empty(&ready) && arrival_ptr < n) {
        current_time = order[arrival_ptr]->arrival_time;
        admit_arrivals(&ready, order, n, &arrival_ptr, current_time);
    }

    while (completed < n) {
        if (is_queue_empty(&ready)) {
            current_time = order[arrival_ptr]->arrival_time;
            admit_arrivals(&ready, order, n, &arrival_ptr, current_time);
            continue;
        }

        PrintJob *job = dequeue(&ready);

        if (job->start_time == -1) {
            job->start_time = current_time;
            job->response_time = job->start_time - job->arrival_time;
        }

        int slice = (job->remaining_pages < quantum) ? job->remaining_pages : quantum;
        current_time += slice;
        job->remaining_pages -= slice;

        /* Newly arrived jobs join the tail ahead of the requeued job,
         * matching the standard textbook Round Robin convention. */
        admit_arrivals(&ready, order, n, &arrival_ptr, current_time);

        if (job->remaining_pages > 0) {
            job->state = JOB_STATE_READY;
            enqueue(&ready, job);
        } else {
            job->completion_time = current_time;
            job->turnaround_time = job->completion_time - job->arrival_time;
            job->waiting_time = job->turnaround_time - job->total_pages;
            job->state = JOB_STATE_COMPLETED;
            completed++;
        }
    }

    free(order);
    return current_time;
}
