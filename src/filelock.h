/* Sanchit Jindal */
#ifndef FILELOCK_H
#define FILELOCK_H

#include <stdio.h>
#include "queue.h"

/* Advisory-locked handle onto the on-disk shared queue log, so that
 * independently executing processes never interleave writes. */
typedef struct {
    FILE *fp;
    int fd;
} FileLock;

FileLock *filelock_open(const char *path);
int filelock_acquire(FileLock *fl);
int filelock_release(FileLock *fl);
void filelock_close(FileLock *fl);

/* Column headings of the shared job log. Written once, as the first line,
 * by whichever process finds the file empty while holding the lock. */
#define JOB_LOG_HEADER \
    "job_id,user_name,arrival_time,total_pages,priority,completion_time,waiting_time,process_id"

/* One line of the job log, as read back by filelock_read_job_log(). */
typedef struct {
    int job_id;
    char user_name[MAX_USERNAME_LEN];
    int arrival_time;
    int total_pages;
    int priority;
    int completion_time;
    int waiting_time;
    int process_id;     /* process that wrote the line (-1 in old logs) */
} LogRecord;

/* Convenience helper: opens path, acquires the lock, appends one CSV line
 * describing job (plus the header if the file is new), releases the lock,
 * and closes the file. */
int filelock_append_job_log(const char *path, const PrintJob *job);

/* Reads the job log under the same lock, so a reader never sees a line
 * that a writer has only half written. Stores up to max records in out[]
 * and returns the total number of records in the file (-1 on error). */
int filelock_read_job_log(const char *path, LogRecord out[], int max);

#endif /* FILELOCK_H */
