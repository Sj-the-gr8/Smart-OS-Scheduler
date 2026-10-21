#ifndef MLQ_H
#define MLQ_H

#include "queue.h"
#include "smart.h"

/* Three-level print spooler. Scheduling decisions are made at every page
 * boundary:
 *
 *   Level 0  DEAN     FCFS. Always served first; a Dean arrival preempts
 *                     any lower-level job at the next page boundary.
 *   Level 1  FACULTY  Round Robin with a page quantum. An urgent Faculty
 *                     job (CSV urgent=1) is run to completion instead of
 *                     being time-sliced; several urgent jobs run in FCFS
 *                     order. Only a Dean job can interrupt an urgent job.
 *   Level 2  STUDENT  Smart Queue, served only when DEAN and FACULTY are
 *                     empty. Starvation turn: once a Student job has
 *                     waited >= tau_fair, the Student queue is given one
 *                     turn ahead of FACULTY and that job runs to
 *                     completion. Student jobs are never promoted.
 *
 * A job's level comes from its user_name: "Dean*" -> DEAN,
 * "Faculty*" or "Staff*" -> FACULTY, anything else -> STUDENT. */

typedef enum {
    LEVEL_DEAN,
    LEVEL_FACULTY,
    LEVEL_STUDENT
} JobLevel;

JobLevel job_level(const PrintJob *job);
const char *level_name(JobLevel level);

/* Deterministic page-by-page simulation of the multilevel policy for the
 * comparison table. Resets and re-simulates jobs in place, returns the
 * makespan. */
int run_multilevel(PrintJob jobs[], int n, const SmartParams *params, int quantum);

/* Live concurrent demo: one submitter thread each for DEAN and FACULTY
 * and two for STUDENT enqueue jobs into the shared queues while a printer
 * thread prints one page per tick. All five threads share one mutex; the
 * printer drives the clock through condition variables, so the results
 * match run_multilevel(). Each completed job is appended to log_path
 * under the file lock. */
void demo_multilevel_concurrent(PrintJob jobs[], int n, const SmartParams *params,
                                int quantum, const char *log_path);

#endif /* MLQ_H */
