/* Sanchit Jindal */
#ifndef SMART_H
#define SMART_H

#include "queue.h"

typedef struct {
    double alpha;   /* Priority weight coefficient */
    double beta;    /* Aging weight coefficient */
    double gamma;   /* Size penalty coefficient */
    int tau_fair;   /* Starvation-OVERRIDE threshold (time units): a job that has
                     * waited >= tau_fair is dispatched ahead of every
                     * non-starved job. Fixed Smart Queue uses the reporting
                     * threshold (2 x avg pages) here; SmartAdaptive replaces
                     * it with a learned value (see adaptive.h). */
} SmartParams;

double compute_smart_score(const PrintJob *job, int current_time, const SmartParams *params);

/* Returns (without removing) the job Smart Queue would dispatch next from
 * ready at current_time. Reused by the multilevel queue's user level. */
PrintJob *select_smart(PrintQueue *ready, int current_time, const SmartParams *params);

/* Resets and re-simulates the given jobs array using the Smart Queue
 * policy and returns the simulation makespan. */
int run_smart_queue(PrintJob jobs[], int n, const SmartParams *params);

#endif /* SMART_H */
