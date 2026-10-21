/* Ananyashree Sharma */
#ifndef QUEUE_H
#define QUEUE_H

#define MAX_USERNAME_LEN 32

typedef enum {
    JOB_STATE_NEW,
    JOB_STATE_READY,
    JOB_STATE_RUNNING,
    JOB_STATE_WAITING,
    JOB_STATE_COMPLETED
} JobState;

typedef struct PrintJob {
    int job_id;                        /* Unique Process/Job Identifier */
    char user_name[MAX_USERNAME_LEN];  /* Owner/UID string */
    int arrival_time;                  /* Time tick at which job enters Ready Queue */
    int total_pages;                   /* Total service demand (Burst Time) */
    int remaining_pages;               /* Remaining service units (for preemptive RR) */
    int priority;                      /* Base urgency level (1=Low, 5=High) */
    int is_urgent;                     /* Optional CSV flag: urgent Faculty job runs to completion */

    /* Dynamic OS tracking fields */
    int start_time;                    /* Clock tick when job first acquired printer */
    int completion_time;               /* Clock tick when job completed execution */
    int waiting_time;                  /* Total time spent idling in Ready Queue */
    int turnaround_time;               /* Total duration: completion_time - arrival */
    int response_time;                 /* Latency: start_time - arrival_time */

    double smart_score;                /* Dynamic score computed by Smart Policy */
    int is_starved;                    /* Flag indicating wait exceeded tau_fair */

    JobState state;                    /* Current process lifecycle state */
    struct PrintJob *next;             /* Intrusive pointer for linked-list queue */
} PrintJob;

typedef struct {
    PrintJob *head;
    PrintJob *tail;
    int size;
} PrintQueue;

void init_queue(PrintQueue *q);
void enqueue(PrintQueue *q, PrintJob *job);
PrintJob *dequeue(PrintQueue *q);
PrintJob *peek(PrintQueue *q);
int is_queue_empty(const PrintQueue *q);
int queue_size(const PrintQueue *q);
PrintJob *remove_job_by_id(PrintQueue *q, int job_id);
void reset_job_for_run(PrintJob *job);

/* Shared by every scheduler (FCFS/SJF/Priority/RR/Smart): resets and
 * returns all n jobs as an array of pointers sorted by arrival_time
 * (ties broken by job_id). Caller must free() the returned array. */
PrintJob **build_arrival_order(PrintJob jobs[], int n);

/* Moves every job from order[*arrival_ptr..] whose arrival_time is
 * <= current_time into the ready queue, advancing *arrival_ptr. */
void admit_arrivals(PrintQueue *ready, PrintJob **order, int n,
                     int *arrival_ptr, int current_time);

#endif /* QUEUE_H */
