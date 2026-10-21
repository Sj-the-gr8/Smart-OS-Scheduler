/* Sanchit Jindal */
#ifndef ADAPTIVE_H
#define ADAPTIVE_H

#include "queue.h"
#include "smart.h"

/* Adaptive Smart Queue: learns the four Smart Queue parameters (alpha,
 * beta, gamma and the starvation-override threshold) from the workload.
 *
 * TWO THRESHOLDS
 * --------------
 * The fixed Smart Queue uses one value, tau_fair, for two different jobs:
 *   (a) the OVERRIDE threshold: a job that has waited >= tau_fair is
 *       dispatched ahead of every non-starved job (see select_smart());
 *   (b) the REPORTING threshold: a job whose wait exceeds tau_fair is
 *       counted as "starved" in the comparison table, for every policy.
 * The adaptive version learns (a) only. (b) stays fixed (fixed->tau_fair)
 * and is the yardstick used both in the objective below and in the
 * comparison table, so SmartAdaptive cannot look better simply by
 * redefining what "starved" means.
 *
 * STAGE 1 - learn from the baseline algorithms
 * --------------------------------------------
 * FCFS, SJF, Priority and Round Robin are simulated on the workload.
 *   size signal       = how much SJF beats FCFS on average wait  -> gamma
 *   priority signal   = how much Priority beats FCFS on
 *                       priority-weighted wait                   -> alpha
 *   starvation signal = largest share of jobs SJF or Priority
 *                       starve                                   -> beta
 *   override threshold = longest wait any job suffers under FCFS
 * With all signals at 0 the weights equal the defaults (1.0, 1.0, 0.5).
 *
 * Why the longest FCFS wait is the override threshold: FCFS never lets a
 * job be overtaken, so it is the reference for "fair order". A job that
 * has already waited longer than the worst wait FCFS would have caused on
 * this same workload is being treated worse than plain fair order - that
 * is the logical definition of starvation. The threshold therefore scales
 * with the load: under light load FCFS waits are short and the override
 * steps in early; under heavy load normal waits are long, so the score
 * (not the override) makes the decisions and only genuine outliers are
 * rescued. The old rule (2 x average job size) ignores load, so on busy
 * workloads nearly every job crossed it and Smart Queue degenerated into
 * oldest-first regardless of the weights.
 *
 * STAGE 2 - refine by simulation
 * ------------------------------
 * Objective (lower is better), always measured with the fixed REPORTING
 * threshold:
 *     priority-weighted average wait + reporting_tau * (starved jobs / n)
 * A coarse grid over alpha, gamma and the override threshold
 * (0.5x .. 1.5x the Stage-1 value) is followed by coordinate descent on
 * alpha, beta and gamma. A candidate is kept only if it lowers the
 * objective. The search starts from whichever of the Stage-1 parameters
 * and the fixed parameters scores better, so the result is never worse
 * than fixed Smart Queue on this objective.
 *
 * Caveat: the parameters are learned on the same trace they are then
 * evaluated on - comparable to calibrating a spooler on its past print
 * logs, not an online scheduler adapting mid-run.
 *
 * Prints the learning trace and returns the learned parameters; the
 * returned tau_fair is the learned OVERRIDE threshold. The jobs array is
 * re-simulated in place. */
SmartParams learn_smart_params(PrintJob jobs[], int n, int quantum, const SmartParams *fixed);

#endif /* ADAPTIVE_H */
