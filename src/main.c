/* Ananyashree Sharma */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "queue.h"
#include "scheduler.h"
#include "smart.h"
#include "metrics.h"
#include "filelock.h"
#include "mlq.h"
#include "sync.h"
#include "adaptive.h"

#define MAX_JOBS 256
#define MAX_LINE 256

/* Reads a CSV workload trace: job_id,user_name,arrival_time,total_pages,priority[,urgent]
 * The urgent column (0/1) is optional and defaults to 0.
 * Blank lines and lines starting with '#' are ignored. */
static int load_workload(const char *path, PrintJob jobs[], int max_jobs) {
    FILE *fp = fopen(path, "r");
    if (fp == NULL) {
        fprintf(stderr, "Error: could not open workload file '%s'\n", path);
        return -1;
    }

    char line[MAX_LINE];
    int count = 0;

    while (fgets(line, sizeof(line), fp) != NULL && count < max_jobs) {
        char *trimmed = line;
        while (*trimmed == ' ' || *trimmed == '\t') {
            trimmed++;
        }
        if (trimmed[0] == '#' || trimmed[0] == '\n' || trimmed[0] == '\r' || trimmed[0] == '\0') {
            continue;
        }

        PrintJob *job = &jobs[count];
        char user_buf[MAX_USERNAME_LEN];
        int urgent = 0;
        int parsed = sscanf(trimmed, "%d,%31[^,],%d,%d,%d,%d",
                             &job->job_id, user_buf, &job->arrival_time,
                             &job->total_pages, &job->priority, &urgent);
        if (parsed != 5 && parsed != 6) {
            fprintf(stderr, "Warning: skipping malformed line: %s", trimmed);
            continue;
        }
        strncpy(job->user_name, user_buf, MAX_USERNAME_LEN - 1);
        job->user_name[MAX_USERNAME_LEN - 1] = '\0';
        job->is_urgent = (urgent != 0);
        reset_job_for_run(job);
        count++;
    }

    fclose(fp);
    return count;
}

static int compute_default_tau_fair(const PrintJob jobs[], int n) {
    if (n == 0) {
        return 1;
    }
    long total = 0;
    for (int i = 0; i < n; i++) {
        total += jobs[i].total_pages;
    }
    double avg = (double)total / n;
    int tau = (int)(2.0 * avg);
    return tau < 1 ? 1 : tau;
}

/* Prints one policy's per-job table and metrics row, then appends the
 * metrics to results[] for the final comparison table. */
static void report_result(const char *label, PrintJob jobs[], int n, int makespan,
                           int tau_fair, Metrics results[], int *result_count) {
    Metrics m = compute_metrics(label, jobs, n, makespan, tau_fair);
    printf("\n--- %s ---\n", label);
    print_job_table(jobs, n);
    print_metrics_row_header();
    print_metrics_row(&m);
    results[(*result_count)++] = m;
}

/* Reads the shared job log back under the file lock and prints it as a
 * table. Rows are in the order jobs finished (the order they were logged);
 * the PID column shows which process wrote each row, so runs from several
 * processes appending to the same log can be told apart. */
static void print_job_log_table(const char *log_path) {
    enum { MAX_LOG_ROWS = 1024 };
    static LogRecord rows[MAX_LOG_ROWS];
    int total = filelock_read_job_log(log_path, rows, MAX_LOG_ROWS);
    if (total < 0) {
        fprintf(stderr, "Warning: could not read '%s'\n", log_path);
        return;
    }
    int shown = total < MAX_LOG_ROWS ? total : MAX_LOG_ROWS;

    printf("\n=== Shared job log '%s' (read back under the file lock) ===\n", log_path);
    printf("One row per completed job, in the order the printer finished them.\n");
    printf("%-5s %-6s %-16s %-8s %-7s %-6s %-5s %-7s %-6s %-6s %-8s\n",
           "Order", "JobID", "User", "Level", "Arrive", "Pages", "Prio",
           "Finish", "Wait", "TAT", "PID");
    for (int i = 0; i < shown; i++) {
        const LogRecord *r = &rows[i];
        PrintJob probe;
        snprintf(probe.user_name, sizeof(probe.user_name), "%s", r->user_name);
        printf("%-5d %-6d %-16s %-8s %-7d %-6d %-5d %-7d %-6d %-6d %-8d\n",
               i + 1, r->job_id, r->user_name, level_name(job_level(&probe)),
               r->arrival_time, r->total_pages, r->priority, r->completion_time,
               r->waiting_time, r->completion_time - r->arrival_time, r->process_id);
    }
    if (total > shown) {
        printf("... %d more row(s) not shown\n", total - shown);
    }
    printf("%d row(s) in the log. Columns in the file: %s\n", total, JOB_LOG_HEADER);
}

int main(int argc, char *argv[]) {
    /* "--demo" may appear anywhere; every other argument is positional. */
    const char *args[8];
    int nargs = 0;
    int demo = 0;
    for (int i = 0; i < argc; i++) {
        if (strcmp(argv[i], "--demo") == 0) {
            demo = 1;
        } else if (nargs < 8) {
            args[nargs++] = argv[i];
        }
    }

    if (nargs < 2) {
        fprintf(stderr, "Usage: %s <workload.csv> [quantum] [tau_fair] [alpha] [beta] [gamma] [--demo]\n", argv[0]);
        fprintf(stderr, "Defaults: quantum=3 tau_fair=<2x avg pages> alpha=1.0 beta=1.0 gamma=0.5\n");
        return EXIT_FAILURE;
    }

    PrintJob jobs[MAX_JOBS];
    int n = load_workload(args[1], jobs, MAX_JOBS);
    if (n <= 0) {
        fprintf(stderr, "Error: no valid jobs loaded from '%s'\n", args[1]);
        return EXIT_FAILURE;
    }
    printf("Loaded %d job(s) from '%s'\n", n, args[1]);

    int quantum = (nargs > 2) ? atoi(args[2]) : 3;
    int tau_fair = (nargs > 3) ? atoi(args[3]) : compute_default_tau_fair(jobs, n);
    SmartParams params;
    params.alpha = (nargs > 4) ? atof(args[4]) : 1.0;
    params.beta = (nargs > 5) ? atof(args[5]) : 1.0;
    params.gamma = (nargs > 6) ? atof(args[6]) : 0.5;
    params.tau_fair = tau_fair;

    printf("Quantum=%d  tau_fair=%d  alpha=%.2f  beta=%.2f  gamma=%.2f\n",
           quantum, tau_fair, params.alpha, params.beta, params.gamma);

    Metrics results[7];
    int result_count = 0;

    report_result("FCFS", jobs, n, run_fcfs(jobs, n), tau_fair, results, &result_count);
    report_result("SJF", jobs, n, run_sjf(jobs, n), tau_fair, results, &result_count);
    report_result("Priority", jobs, n, run_priority(jobs, n), tau_fair, results, &result_count);
    report_result("RoundRobin", jobs, n, run_round_robin(jobs, n, quantum), tau_fair, results, &result_count);
    report_result("SmartQueue", jobs, n, run_smart_queue(jobs, n, &params), tau_fair, results, &result_count);

    /* learned.tau_fair is the learned OVERRIDE threshold; the table keeps
     * counting starvation with the fixed reporting threshold tau_fair, so
     * every policy is judged by the same yardstick (see adaptive.h). */
    SmartParams learned = learn_smart_params(jobs, n, quantum, &params);
    report_result("SmartAdaptive", jobs, n, run_smart_queue(jobs, n, &learned), tau_fair, results, &result_count);
    report_result("MultiLevel", jobs, n, run_multilevel(jobs, n, &params, quantum), tau_fair, results, &result_count);

    print_comparison_table(results, result_count);
    print_score_ranking(results, result_count, tau_fair);

    /* Optional live demonstration of the concurrency (mutex + condition
     * variables on the shared DEAN/FACULTY/STUDENT queues) and
     * file-locking subsystems, gated behind --demo so the default run
     * stays focused on scheduler comparison output. */
    if (demo) {
        printf("\n=== Mutex Demo: race condition on a shared print queue, without vs with a mutex ===\n");
        demo_mutex_race(4, 2000);

        const char *log_path = "queue_log.csv";
        printf("\n=== Multilevel Queue Demo: Dean / Faculty / Student queues, concurrent submitters, file-locked log ===\n");
        demo_multilevel_concurrent(jobs, n, &params, quantum, log_path);
        printf("\nAll %d job(s) printed and appended to '%s' under advisory lock.\n", n, log_path);
        print_job_log_table(log_path);
    }

    return EXIT_SUCCESS;
}
