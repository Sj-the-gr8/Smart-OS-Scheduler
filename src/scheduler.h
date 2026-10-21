/* Ananyashree Sharma */
#ifndef SCHEDULER_H
#define SCHEDULER_H

#include "queue.h"

/* Each dispatcher resets and re-simulates the given jobs array in place
 * (dynamic fields only; arrival_time/total_pages/priority/job_id are
 * untouched) and returns the simulation makespan (total elapsed time). */
int run_fcfs(PrintJob jobs[], int n);
int run_sjf(PrintJob jobs[], int n);
int run_priority(PrintJob jobs[], int n);
int run_round_robin(PrintJob jobs[], int n, int quantum);

/* Returns (without removing) the highest-priority job in ready, tie-broken
 * by earliest arrival then lowest job_id. Reused by the multilevel queue. */
PrintJob *select_priority(PrintQueue *ready);

#endif /* SCHEDULER_H */
