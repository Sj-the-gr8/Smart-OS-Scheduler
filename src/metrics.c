/* Sanchit Jindal */
#include <stdio.h>
#include <string.h>
#include "metrics.h"

Metrics compute_metrics(const char *policy_name, PrintJob jobs[], int n,
                         int makespan, int tau_fair) {
    Metrics m;
    memset(&m, 0, sizeof(m));
    strncpy(m.policy_name, policy_name, sizeof(m.policy_name) - 1);

    long sum_wait = 0;
    long sum_tat = 0;
    long sum_resp = 0;
    long total_pages = 0;
    int starved = 0;
    double weighted_wait = 0.0;
    double weight_total = 0.0;
    int over_tau = 0;           /* wait > tau_fair, for the score's penalty */

    for (int i = 0; i < n; i++) {
        sum_wait += jobs[i].waiting_time;
        sum_tat += jobs[i].turnaround_time;
        sum_resp += jobs[i].response_time;
        total_pages += jobs[i].total_pages;

        double w = jobs[i].priority > 0 ? jobs[i].priority : 1;
        weighted_wait += w * jobs[i].waiting_time;
        weight_total += w;
        if (jobs[i].waiting_time > m.max_wait) {
            m.max_wait = jobs[i].waiting_time;
        }
        if (jobs[i].waiting_time > tau_fair) {
            over_tau++;
        }

        if (jobs[i].waiting_time > tau_fair) {
            jobs[i].is_starved = 1;
        }
        if (jobs[i].is_starved) {
            starved++;
        }
    }

    m.avg_waiting_time = n > 0 ? (double)sum_wait / n : 0.0;
    m.avg_turnaround_time = n > 0 ? (double)sum_tat / n : 0.0;
    m.avg_response_time = n > 0 ? (double)sum_resp / n : 0.0;
    m.throughput = makespan > 0 ? (double)n / makespan : 0.0;
    m.utilization_pct = makespan > 0 ? ((double)total_pages / makespan) * 100.0 : 0.0;
    m.starvation_count = starved;
    m.makespan = makespan;
    m.prio_weighted_wait = weight_total > 0 ? weighted_wait / weight_total : 0.0;
    m.score = m.prio_weighted_wait + (n > 0 ? (double)tau_fair * over_tau / n : 0.0);

    return m;
}

void print_job_table(const PrintJob jobs[], int n) {
    printf("%-6s %-14s %-8s %-6s %-6s %-8s %-8s %-8s %-8s %-8s %-9s\n",
           "JobID", "User", "Arrive", "Pages", "Prio", "Start", "Finish",
           "Wait", "TAT", "Resp", "Starved?");
    for (int i = 0; i < n; i++) {
        const PrintJob *j = &jobs[i];
        printf("%-6d %-14s %-8d %-6d %-6d %-8d %-8d %-8d %-8d %-8d %-9s\n",
               j->job_id, j->user_name, j->arrival_time, j->total_pages,
               j->priority, j->start_time, j->completion_time,
               j->waiting_time, j->turnaround_time, j->response_time,
               j->is_starved ? "YES" : "no");
    }
}

void print_metrics_row_header(void) {
    printf("%-14s %10s %10s %10s %10s %10s %10s %10s %10s %10s\n",
           "Policy", "AvgWait", "AvgTAT", "AvgResp", "Throughput",
           "Util(%)", "Starved", "MaxWait", "PrioWait", "Score");
}

void print_metrics_row(const Metrics *m) {
    printf("%-14s %10.2f %10.2f %10.2f %10.4f %10.2f %10d %10d %10.2f %10.2f\n",
           m->policy_name, m->avg_waiting_time, m->avg_turnaround_time,
           m->avg_response_time, m->throughput, m->utilization_pct,
           m->starvation_count, m->max_wait, m->prio_weighted_wait, m->score);
}

void print_comparison_table(const Metrics metrics[], int count) {
    printf("\n=== Comparative Evaluation Table (Section 13) ===\n");
    print_metrics_row_header();
    for (int i = 0; i < count; i++) {
        print_metrics_row(&metrics[i]);
    }
}

void print_score_ranking(const Metrics metrics[], int count, int tau_fair) {
    int order[32];
    if (count > 32) {
        count = 32;
    }
    for (int i = 0; i < count; i++) {
        order[i] = i;
    }
    /* Insertion sort by score, ascending; equal scores keep table order. */
    for (int i = 1; i < count; i++) {
        int k = order[i];
        int j = i - 1;
        while (j >= 0 && metrics[order[j]].score > metrics[k].score) {
            order[j + 1] = order[j];
            j--;
        }
        order[j + 1] = k;
    }

    double best = count > 0 ? metrics[order[0]].score : 0.0;
    printf("\n=== Score Ranking (lower is better) ===\n");
    printf("Score = PrioWait + tau_fair x (share of jobs with wait > tau_fair), tau_fair = %d\n", tau_fair);
    printf("%-5s %-14s %10s %10s %8s %10s\n", "Rank", "Policy", "Score", "x Best", "Starved", "PrioWait");
    int rank = 0;
    for (int i = 0; i < count; i++) {
        const Metrics *m = &metrics[order[i]];
        /* Scores within 0.5% of the one above share its rank (a tie). */
        if (i == 0 || m->score > metrics[order[i - 1]].score * 1.005) {
            rank = i + 1;
        }
        double ratio = best > 0.0 ? m->score / best : 1.0;
        printf("%-5d %-14s %10.2f %9.2fx %8d %10.2f%s\n", rank, m->policy_name, m->score,
               ratio, m->starvation_count, m->prio_weighted_wait,
               ratio < 1.005 ? "   <- best" : "");
    }
}
