/* Sanchit Jindal */
#include <stdlib.h>
#include "smart.h"

/* Smart Score_i = (alpha * P_i) + (beta * W_i) - (gamma * S_i)
 * See Section 9 of the Review-1 report for the full derivation. */
double compute_smart_score(const PrintJob *job, int current_time, const SmartParams *params) {
    double wait = (double)(current_time - job->arrival_time);
    return (params->alpha * job->priority) + (params->beta * wait) - (params->gamma * job->total_pages);
}

/* Among the ready queue, picks the job that has breached tau_fair (FCFS
 * order among starved jobs); if none has starved, picks the highest
 * Smart Score, tie-broken by job_id. */
PrintJob *select_smart(PrintQueue *ready, int current_time, const SmartParams *params) {
    PrintJob *oldest_starved = NULL;
    for (PrintJob *cur = ready->head; cur != NULL; cur = cur->next) {
        int wait = current_time - cur->arrival_time;
        if (wait >= params->tau_fair) {
            if (oldest_starved == NULL ||
                cur->arrival_time < oldest_starved->arrival_time ||
                (cur->arrival_time == oldest_starved->arrival_time && cur->job_id < oldest_starved->job_id)) {
                oldest_starved = cur;
            }
        }
    }
    if (oldest_starved != NULL) {
        oldest_starved->is_starved = 1;
        oldest_starved->smart_score = compute_smart_score(oldest_starved, current_time, params);
        return oldest_starved;
    }

    PrintJob *best = NULL;
    double best_score = 0.0;
    for (PrintJob *cur = ready->head; cur != NULL; cur = cur->next) {
        double score = compute_smart_score(cur, current_time, params);
        cur->smart_score = score;
        if (best == NULL || score > best_score ||
            (score == best_score && cur->job_id < best->job_id)) {
            best = cur;
            best_score = score;
        }
    }
    return best;
}

int run_smart_queue(PrintJob jobs[], int n, const SmartParams *params) {
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

        PrintJob *job = select_smart(&ready, current_time, params);
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
