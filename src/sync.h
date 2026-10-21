/* Sanchit Jindal */
#ifndef SYNC_H
#define SYNC_H

/* Mutex demonstration: why the shared print queue needs mutual exclusion.
 *
 * n_threads submitter threads each enqueue jobs_per_thread print jobs into
 * ONE shared PrintQueue. The experiment runs twice:
 *   1. WITHOUT the mutex - threads interleave inside enqueue() and jobs are
 *      lost (race condition / lost update);
 *   2. WITH a pthread mutex around enqueue() - the critical section runs
 *      one thread at a time and every job arrives.
 * For each run it reports the expected job count, the queue's size field,
 * the number of jobs actually reachable from head, and (with the mutex) how
 * often a thread found the mutex held and had to block. */
void demo_mutex_race(int n_threads, int jobs_per_thread);

#endif /* SYNC_H */
