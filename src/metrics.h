/* Sanchit Jindal */
#ifndef METRICS_H
#define METRICS_H

#include "queue.h"

typedef struct {
    char policy_name[24];
    double avg_waiting_time;
    double avg_turnaround_time;
    double avg_response_time;
    double throughput;
    double utilization_pct;
    int starvation_count;
    int makespan;

    /* Score: the single number used to compare policies (lower is better).
     *   score = prio_weighted_wait + tau_fair * (jobs with wait > tau_fair) / n
     * prio_weighted_wait is the average wait with each job weighted by its
     * priority (1-5), so delaying an important job costs more. The penalty
     * term charges tau_fair for every starved job. It is the same objective
     * SmartAdaptive minimises (adaptive.c), so its learned score and the
     * table's score agree. */
    int max_wait;
    double prio_weighted_wait;
    double score;
} Metrics;

/* Computes all Section-13 performance metrics from a completed jobs array.
 * tau_fair is the fairness boundary used for the starvation count (Sc)
 * and for the score's starvation penalty. */
Metrics compute_metrics(const char *policy_name, PrintJob jobs[], int n,
                         int makespan, int tau_fair);

void print_job_table(const PrintJob jobs[], int n);
void print_metrics_row_header(void);
void print_metrics_row(const Metrics *m);
void print_comparison_table(const Metrics metrics[], int count);

/* Prints the policies ranked by score, each with its score as a multiple
 * of the best one, and the formula used. */
void print_score_ranking(const Metrics metrics[], int count, int tau_fair);

#endif /* METRICS_H */
