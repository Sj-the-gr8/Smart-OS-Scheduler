/* Sanchit Jindal */
#ifndef _WIN32
#define _POSIX_C_SOURCE 200809L /* exposes fileno()/flock() under -std=c11 */
#endif

#include <stdio.h>
#include <stdlib.h>
#include "filelock.h"

#ifdef _WIN32
#include <io.h>
#include <process.h>
#include <sys/locking.h>
#define getpid _getpid
#else
#include <unistd.h>
#include <sys/file.h>
#endif

FileLock *filelock_open(const char *path) {
    FILE *fp = fopen(path, "a+");
    if (fp == NULL) {
        return NULL;
    }

    FileLock *fl = malloc(sizeof(FileLock));
    if (fl == NULL) {
        fclose(fp);
        return NULL;
    }

    fl->fp = fp;
#ifdef _WIN32
    fl->fd = _fileno(fp);
#else
    fl->fd = fileno(fp);
#endif
    return fl;
}

/* Acquires an exclusive advisory lock on the shared queue log so that no
 * two processes interleave writes to it */
int filelock_acquire(FileLock *fl) {
    if (fl == NULL) {
        return -1;
    }
#ifdef _WIN32
    fseek(fl->fp, 0, SEEK_SET);
    return _locking(fl->fd, _LK_LOCK, 1);
#else
    return flock(fl->fd, LOCK_EX);
#endif
}

int filelock_release(FileLock *fl) {
    if (fl == NULL) {
        return -1;
    }
#ifdef _WIN32
    fseek(fl->fp, 0, SEEK_SET);
    return _locking(fl->fd, _LK_UNLCK, 1);
#else
    return flock(fl->fd, LOCK_UN);
#endif
}

void filelock_close(FileLock *fl) {
    if (fl == NULL) {
        return;
    }
    fclose(fl->fp);
    free(fl);
}

int filelock_append_job_log(const char *path, const PrintJob *job) {
    FileLock *fl = filelock_open(path);
    if (fl == NULL) {
        return -1;
    }

    if (filelock_acquire(fl) != 0) {
        filelock_close(fl);
        return -1;
    }

    fseek(fl->fp, 0, SEEK_END);
    /* Checked while holding the lock, so only one process writes it. */
    if (ftell(fl->fp) == 0) {
        fprintf(fl->fp, "%s\n", JOB_LOG_HEADER);
    }
    fprintf(fl->fp, "%d,%s,%d,%d,%d,%d,%d,%d\n",
            job->job_id, job->user_name, job->arrival_time, job->total_pages,
            job->priority, job->completion_time, job->waiting_time, (int)getpid());
    fflush(fl->fp);

    filelock_release(fl);
    filelock_close(fl);
    return 0;
}

int filelock_read_job_log(const char *path, LogRecord out[], int max) {
    FileLock *fl = filelock_open(path);
    if (fl == NULL) {
        return -1;
    }
    if (filelock_acquire(fl) != 0) {
        filelock_close(fl);
        return -1;
    }

    fseek(fl->fp, 0, SEEK_SET);
    char line[256];
    int total = 0;
    while (fgets(line, sizeof(line), fl->fp) != NULL) {
        LogRecord r;
        r.process_id = -1;
        int parsed = sscanf(line, "%d,%31[^,],%d,%d,%d,%d,%d,%d",
                            &r.job_id, r.user_name, &r.arrival_time, &r.total_pages,
                            &r.priority, &r.completion_time, &r.waiting_time,
                            &r.process_id);
        if (parsed < 7) {
            continue;   /* header line or malformed */
        }
        if (total < max) {
            out[total] = r;
        }
        total++;
    }

    filelock_release(fl);
    filelock_close(fl);
    return total;
}
