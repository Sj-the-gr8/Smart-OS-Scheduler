/* Sanchit Jindal */
/* Adaptive Smart Queue - see adaptive.h for the full design rationale
 * (two thresholds, Stage 1 signals, Stage 2 search). */
#include <stdio.h>
#include "adaptive.h"
#include "scheduler.h"

#define MIN_WEIGHT 0.01
#define MAX_WEIGHT 100.0
#define MAX_ROUNDS 40

/* Summary of one simulated run, measured against the fixed REPORTING
 * threshold (never against the learned override threshold). */
typedef struct {
    double avg_wait;
    double weighted_wait;   /* average wait, each job weighted by its priority */
    double starved_frac;    /* share of jobs whose wait exceeded reporting_tau */
    int max_wait;           /* longest wait of any job */
    double objective;       /* weighted_wait + reporting_tau * starved_frac */
} RunStats;

static RunStats measure(const PrintJob jobs[], int n, int reporting_tau) {
    RunStats r = { 0.0, 0.0, 0.0, 0, 0.0 };
    double wait_sum = 0.0;
    double weighted_sum = 0.0;
    double weight_total = 0.0;
    int starved = 0;

    for (int i = 0; i < n; i++) {
        double w = jobs[i].priority > 0 ? jobs[i].priority : 1;
        wait_sum += jobs[i].waiting_time;
        weighted_sum += w * jobs[i].waiting_time;
        weight_total += w;
        if (jobs[i].waiting_time > reporting_tau) {
            starved++;
        }
        if (jobs[i].waiting_time > r.max_wait) {
            r.max_wait = jobs[i].waiting_time;
        }
    }
    if (n > 0) {
        r.avg_wait = wait_sum / n;
        r.weighted_wait = weighted_sum / weight_total;
        r.starved_frac = (double)starved / n;
    }
    r.objective = r.weighted_wait + reporting_tau * r.starved_frac;
    return r;
}

/* Simulates Smart Queue with candidate parameters p (p->tau_fair is the
 * override threshold) and scores it against the reporting threshold. */
static double evaluate(PrintJob jobs[], int n, const SmartParams *p, int reporting_tau) {
    run_smart_queue(jobs, n, p);
    return measure(jobs, n, reporting_tau).objective;
}

/* Fraction by which `better` improves on `base` (0 if it does not). */
static double improvement(double base, double better) {
    if (base <= 0.0 || better >= base) {
        return 0.0;
    }
    return (base - better) / base;
}

static double *weight_at(SmartParams *p, int k) {
    switch (k) {
    case 0:  return &p->alpha;
    case 1:  return &p->beta;
    default: return &p->gamma;
    }
}

static void print_stats_row(const char *name, const RunStats *r) {
    printf("  %-12s %9.2f %9.2f %8.0f%% %8d %10.2f\n", name, r->avg_wait,
           r->weighted_wait, r->starved_frac * 100.0, r->max_wait, r->objective);
}

SmartParams learn_smart_params(PrintJob jobs[], int n, int quantum, const SmartParams *fixed) {
    const int reporting_tau = fixed->tau_fair;
    static const char *const names[3] = { "alpha", "beta", "gamma" };

    printf("\n=== Adaptive SmartQueue: learning alpha/beta/gamma and the override threshold ===\n");

    /* ---- Stage 1: learn from the baseline algorithms ---------------- */
    RunStats fcfs, sjf, prio, rr;
    run_fcfs(jobs, n);
    fcfs = measure(jobs, n, reporting_tau);
    run_sjf(jobs, n);
    sjf = measure(jobs, n, reporting_tau);
    run_priority(jobs, n);
    prio = measure(jobs, n, reporting_tau);
    run_round_robin(jobs, n, quantum);
    rr = measure(jobs, n, reporting_tau);

    printf("Stage 1 - baseline results (reporting tau_fair=%d; objective = PrioWait + tau_fair x starved share)\n",
           reporting_tau);
    printf("  %-12s %9s %9s %9s %8s %10s\n", "Baseline", "AvgWait", "PrioWait", "Starved", "MaxWait", "Objective");
    print_stats_row("FCFS", &fcfs);
    print_stats_row("SJF", &sjf);
    print_stats_row("Priority", &prio);
    print_stats_row("RoundRobin", &rr);

    double size_signal = improvement(fcfs.avg_wait, sjf.avg_wait);
    double prio_signal = improvement(fcfs.weighted_wait, prio.weighted_wait);
    double starve_signal = sjf.starved_frac > prio.starved_frac ? sjf.starved_frac : prio.starved_frac;

    SmartParams init = *fixed;
    init.alpha = 1.0 + 4.0 * prio_signal;
    init.beta = 1.0 + 2.0 * starve_signal;
    init.gamma = 0.5 + 1.5 * size_signal;
    /* Override threshold = worst wait under fair (FCFS) order; see
     * adaptive.h. Kept >= 1 so a job must wait at least one tick. */
    init.tau_fair = fcfs.max_wait > 1 ? fcfs.max_wait : 1;

    printf("  size signal       %.2f  (SJF vs FCFS avg wait)           -> gamma = 0.5 + 1.5 x %.2f = %.2f\n",
           size_signal, size_signal, init.gamma);
    printf("  priority signal   %.2f  (Priority vs FCFS prio wait)     -> alpha = 1.0 + 4.0 x %.2f = %.2f\n",
           prio_signal, prio_signal, init.alpha);
    printf("  starvation signal %.2f  (worst starved share SJF/Prio)   -> beta  = 1.0 + 2.0 x %.2f = %.2f\n",
           starve_signal, starve_signal, init.beta);
    printf("  override threshold      (longest FCFS wait)              -> tau_override = %d (fixed rule gave %d)\n",
           init.tau_fair, fixed->tau_fair);

    /* ---- Stage 2: refine by simulation ------------------------------ */
    /* Start from whichever of the Stage-1 and fixed parameters scores
     * better, so the learned result can never be worse than fixed. */
    double fixed_obj = evaluate(jobs, n, fixed, reporting_tau);
    double init_obj = evaluate(jobs, n, &init, reporting_tau);
    SmartParams best = init_obj <= fixed_obj ? init : *fixed;
    double best_obj = init_obj <= fixed_obj ? init_obj : fixed_obj;
    int changes = 0;

    printf("Stage 2 - refine by simulation (objective: fixed %.2f, Stage-1 %.2f; starting from %s)\n",
           fixed_obj, init_obj, init_obj <= fixed_obj ? "Stage-1" : "fixed");

    /* 2a. Coarse grid over alpha, gamma and the override threshold, with
     * beta held at its starting value. Scaling alpha, beta and gamma
     * together never changes which job has the highest score, so varying
     * alpha and gamma relative to beta covers every distinct weighting.
     * The override threshold is searched in a band around the Stage-1
     * value (0.5x .. 1.5x the longest FCFS wait) so it keeps its meaning.
     * The grid is needed because single small steps (2b) get stuck on
     * plateaus where no pair of jobs swaps places. */
    static const double weight_grid[] = { 0.0, 0.25, 0.5, 1.0, 2.0, 4.0, 8.0 };
    static const double tau_grid[] = { 0.5, 0.75, 1.0, 1.25, 1.5 };
    const int weight_n = (int)(sizeof(weight_grid) / sizeof(weight_grid[0]));
    const int tau_n = (int)(sizeof(tau_grid) / sizeof(tau_grid[0]));

    SmartParams grid_best = best;
    double grid_obj = best_obj;
    for (int t = 0; t < tau_n; t++) {
        int tau = (int)(tau_grid[t] * init.tau_fair + 0.5);
        for (int a = 0; a < weight_n; a++) {
            for (int g = 0; g < weight_n; g++) {
                SmartParams cand = best;
                cand.alpha = weight_grid[a] * best.beta;
                cand.gamma = weight_grid[g] * best.beta;
                cand.tau_fair = tau > 1 ? tau : 1;
                double obj = evaluate(jobs, n, &cand, reporting_tau);
                if (obj < grid_obj - 1e-9) {
                    grid_best = cand;
                    grid_obj = obj;
                }
            }
        }
    }
    if (grid_obj < best_obj) {
        changes++;
        printf("  grid    alpha %.2f -> %.2f, gamma %.2f -> %.2f, tau_override %d -> %d   objective %.2f -> %.2f\n",
               best.alpha, grid_best.alpha, best.gamma, grid_best.gamma,
               best.tau_fair, grid_best.tau_fair, best_obj, grid_obj);
        best = grid_best;
        best_obj = grid_obj;
    }

    /* 2b. Coordinate descent on alpha, beta and gamma (override threshold
     * now fixed): try scaling one weight up or down by `step`, keep the
     * first change that lowers the objective, and halve the step once no
     * single change helps. */
    double step = 2.0;
    for (int round = 0; round < MAX_ROUNDS && step > 1.05; round++) {
        int improved = 0;
        for (int k = 0; k < 3; k++) {
            for (int dir = 0; dir < 2; dir++) {
                SmartParams cand = best;
                double *w = weight_at(&cand, k);
                double old = *w;
                *w = dir == 0 ? old * step : old / step;
                if (*w < MIN_WEIGHT) *w = MIN_WEIGHT;
                if (*w > MAX_WEIGHT) *w = MAX_WEIGHT;
                if (*w == old) {
                    continue;
                }
                double obj = evaluate(jobs, n, &cand, reporting_tau);
                if (obj < best_obj - 1e-9) {
                    printf("  step %-2d %-5s %6.2f -> %-6.2f objective %7.2f -> %.2f\n",
                           ++changes, names[k], old, *w, best_obj, obj);
                    best = cand;
                    best_obj = obj;
                    improved = 1;
                    break;
                }
            }
        }
        if (!improved) {
            step = 1.0 + (step - 1.0) / 2.0;
        }
    }
    if (changes == 0) {
        printf("  no change improved the objective; keeping the starting parameters\n");
    }

    printf("Learned: alpha=%.2f beta=%.2f gamma=%.2f tau_override=%d  objective %.2f (fixed: %.2f)\n",
           best.alpha, best.beta, best.gamma, best.tau_fair, best_obj, fixed_obj);
    return best;
}
