/* Ananyashree Sharma */
#include <stddef.h>
#include <stdlib.h>
#include "queue.h"

void init_queue(PrintQueue *q) {
    q->head = NULL;
    q->tail = NULL;
    q->size = 0;
}

void enqueue(PrintQueue *q, PrintJob *job) {
    job->next = NULL;
    if (q->tail == NULL) {
        q->head = job;
        q->tail = job;
    } else {
        q->tail->next = job;
        q->tail = job;
    }
    q->size++;
}

PrintJob *dequeue(PrintQueue *q) {
    if (q->head == NULL) {
        return NULL;
    }
    PrintJob *job = q->head;
    q->head = q->head->next;
    if (q->head == NULL) {
        q->tail = NULL;
    }
    job->next = NULL;
    q->size--;
    return job;
}

PrintJob *peek(PrintQueue *q) {
    return q->head;
}

int is_queue_empty(const PrintQueue *q) {
    return q->size == 0;
}

int queue_size(const PrintQueue *q) {
    return q->size;
}

/* Removes a specific job (by ID) from anywhere in the queue, preserving
 * order of the remaining nodes. Returns the removed node, or NULL if not found. */
PrintJob *remove_job_by_id(PrintQueue *q, int job_id) {
    PrintJob *prev = NULL;
    PrintJob *cur = q->head;

    while (cur != NULL) {
        if (cur->job_id == job_id) {
            if (prev == NULL) {
                q->head = cur->next;
            } else {
                prev->next = cur->next;
            }
            if (cur == q->tail) {
                q->tail = prev;
            }
            cur->next = NULL;
            q->size--;
            return cur;
        }
        prev = cur;
        cur = cur->next;
    }
    return NULL;
}

/* Clears the dynamic OS-tracking fields so the same workload array can be
 * re-simulated under a different scheduling policy. */
void reset_job_for_run(PrintJob *job) {
    job->remaining_pages = job->total_pages;
    job->start_time = -1;
    job->completion_time = -1;
    job->waiting_time = 0;
    job->turnaround_time = 0;
    job->response_time = 0;
    job->smart_score = 0.0;
    job->is_starved = 0;
    job->state = JOB_STATE_NEW;
    job->next = NULL;
}

/* Sorts by arrival_time ascending, tie-broken by job_id ascending, so
 * simultaneous arrivals resolve deterministically. */
static int compare_arrival(const void *a, const void *b) {
    PrintJob *ja = *(PrintJob * const *)a;
    PrintJob *jb = *(PrintJob * const *)b;
    if (ja->arrival_time != jb->arrival_time) {
        return ja->arrival_time - jb->arrival_time;
    }
    return ja->job_id - jb->job_id;
}

PrintJob **build_arrival_order(PrintJob jobs[], int n) {
    PrintJob **order = malloc((size_t)n * sizeof(PrintJob *));
    for (int i = 0; i < n; i++) {
        reset_job_for_run(&jobs[i]);
        order[i] = &jobs[i];
    }
    qsort(order, (size_t)n, sizeof(PrintJob *), compare_arrival);
    return order;
}

void admit_arrivals(PrintQueue *ready, PrintJob **order, int n,
                     int *arrival_ptr, int current_time) {
    while (*arrival_ptr < n && order[*arrival_ptr]->arrival_time <= current_time) {
        PrintJob *job = order[*arrival_ptr];
        job->state = JOB_STATE_READY;
        enqueue(ready, job);
        (*arrival_ptr)++;
    }
}
