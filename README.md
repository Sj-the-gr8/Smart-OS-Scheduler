# Smart OS Print Scheduler

A print-spooler simulator in C that compares seven scheduling policies on the same print jobs, including **SmartAdaptive**, a Smart Queue policy that learns its own weights from each workload. It also demonstrates mutual exclusion with POSIX threads, condition variables and file locking.

On the evaluation score, SmartAdaptive is the best or tied-best policy on 8 of 9 workloads, including 200 real jobs from a university cluster log.

## Quick start

Build and run in Linux or WSL (Ubuntu):

```bash
make            # build build/smartprint
make run        # all policies on the convoy workload
make demo       # race-condition demo + threaded multilevel spooler + shared log
make contention # burst workload: mutex and printer contention
make real       # all policies on the real KTH workload
make dashboard  # run every workload and build dashboard/index.html
make clean
```

Requirements: GCC, GNU Make, POSIX threads. `make real` and `make dashboard` also need Python 3 (standard library only).

## Usage

```
./build/smartprint <workload.csv> [quantum] [tau_fair] [alpha] [beta] [gamma] [--demo]
```

| Argument | Default | Meaning |
| --- | --- | --- |
| `workload.csv` | required | Jobs to schedule (format below) |
| `quantum` | 3 | Round Robin time slice, in pages |
| `tau_fair` | 2 × average pages | Starvation threshold for reporting |
| `alpha` `beta` `gamma` | 1.0 1.0 0.5 | Fixed Smart Queue weights |
| `--demo` | off | Also run the concurrency demos; may appear anywhere |

The program prints a job table per policy, a comparison table of all policies, and a score ranking.

## Scheduling policies

| Policy | Type | Rule |
| --- | --- | --- |
| FCFS | Non-preemptive | Earliest arrival first |
| SJF | Non-preemptive | Fewest pages first |
| Priority | Non-preemptive | Highest priority (1–5) first |
| Round Robin | Preemptive | Turns of `quantum` pages |
| Smart Queue | Non-preemptive | Highest score α·Priority + β·Wait − γ·Pages; a job waiting ≥ τ is served first |
| SmartAdaptive | Non-preemptive | Smart Queue with α, β, γ and τ learned from the workload |
| MultiLevel | Preemptive at page boundaries | Dean: FCFS · Faculty/Staff: Round Robin, urgent jobs run to completion · Student: Smart Queue with a starvation turn |

**SmartAdaptive** (`src/adaptive.c`) learns in two stages:

1. **From the baseline policies:** runs FCFS, SJF, Priority and Round Robin, and turns their results into a size signal (raises γ), a priority signal (raises α) and a starvation signal (raises β). The override threshold starts at the longest wait under FCFS.
2. **By simulation:** a grid search, then fine coordinate descent, keeping only changes that lower the score. It never ends up worse than the fixed weights.

The full rationale is in `src/adaptive.h`.

## Evaluation score

Every policy is ranked by one number, in pages, where lower is better:

```
Score = priority-weighted average wait + τ × (share of jobs that waited longer than τ)
```

- **Priority-weighted wait:** delaying an important job costs more.
- **Penalty term:** stops a policy from winning on average by starving a few jobs.
- **τ:** 2 × average pages for every policy, so no policy can redefine starvation.

The score is printed in the `Score` column and the **Score Ranking** table. SmartAdaptive minimises this same score, so its learned score matches the table.

## Concurrency (`--demo`)

| Demo | Shows |
| --- | --- |
| Race condition (`src/sync.c`) | 4 threads × 2,000 enqueues into one queue: thousands of jobs lost without a mutex, none with it |
| Threaded spooler (`src/mlq.c`) | 4 submitter threads (Dean, Faculty, two Students) and a printer thread share the three queues through one `pthread_mutex_t` and two condition variables (`tick`, `job_ready`). Prints each thread blocked on the mutex and each time queues compete for the printer. |
| Shared job log (`src/filelock.c`) | Each finished job is appended to `queue_log.csv` under an exclusive `flock`, with the writer's process ID, then read back under the lock and printed as a table |

The threaded spooler's completion times match the deterministic MultiLevel simulation exactly.

## Workloads

CSV, one job per line; lines starting with `#` are comments:

```
job_id,user_name,arrival_time,total_pages,priority[,urgent]
```

The user name's prefix sets the multilevel level: `Dean*`, `Faculty*` or `Staff*`, or anything else for Student. `urgent` (0/1) is optional.

| File | Jobs | Tests |
| --- | --- | --- |
| `test_a_monotonic.csv` | 5 | Sizes 2–50 pages arriving together |
| `test_b_convoy.csv` | 6 | A 50-page job ahead of five 1-page jobs |
| `test_c_priorities.csv` | 6 | Mixed sizes and priorities |
| `test_d_simultaneous.csv` | 10 | Ten jobs at t = 0 |
| `test_e_short_inflow.csv` | 21 | A long job among a stream of short ones |
| `test_f_batch.csv` | 32 | A heavily loaded printer |
| `test_g_multilevel.csv` | 13 | Every multilevel rule |
| `test_h_contention.csv` | 24 | Same-tick bursts from all levels |
| `real_kth_sp2.csv` | 200 | Real jobs from the KTH SP2 log |

## Real dataset

`workloads/real_kth_sp2.csv` is converted from the **KTH SP2** log of the [Parallel Workloads Archive](https://www.cs.huji.ac.il/labs/parallel/workload/l_kth_sp2/index.html): 28,476 jobs from the Swedish Royal Institute of Technology, 1996–97. `tools/swf_to_workload.py` keeps completed jobs and maps them as follows:

| KTH SP2 field | Print job field |
| --- | --- |
| Job Number | `job_id`, renumbered 1–200 |
| Submit Time | `arrival_time`, rescaled to 90% printer load |
| Run Time | `total_pages`, 300 s per page, capped at 100 |
| Requested Time | `priority`, shortest fifth = 5 |
| User ID | `user_name`: Dean, Faculty or Student by ID (assumed; the log has no roles) |

The workload log from the KTH IBM SP2 was graciously provided by Lars Malinowsky. Archive citation: D. G. Feitelson, D. Tsafrir, D. Krakov, "Experience with using the Parallel Workloads Archive", *J. Parallel and Distributed Computing* 74(10), 2014.

## Dashboard

`make dashboard` runs every workload, saves the output to `dashboard/runs/`, and builds `dashboard/data.js` plus two CSVs with `tools/build_dashboard_data.py`. Open `dashboard/index.html` in any browser; it works offline. It shows the score scoreboard, fixed vs learned weights, SmartAdaptive vs SJF, and the settings learned for each workload. Re-run `make dashboard` after changing the code or a workload.

## Project structure

```
src/
  main.c          driver: loads a workload, runs every policy, prints reports
  queue.c/.h      PrintJob record, linked-list PrintQueue, arrival ordering
  scheduler.c/.h  FCFS, SJF, Priority, Round Robin
  smart.c/.h      Smart Queue scoring and starvation override
  adaptive.c/.h   SmartAdaptive learner
  mlq.c/.h        multilevel queue and threaded spooler demo
  sync.c/.h       race-condition demo
  filelock.c/.h   file-locked shared job log
  metrics.c/.h    metrics, score, comparison and ranking tables
tools/
  swf_to_workload.py       KTH SP2 log -> workload CSV
  build_dashboard_data.py  program output -> dashboard data
workloads/        test workloads and the converted real workload
datasets/         original KTH SP2 log (needed by make real/dashboard)
dashboard/        local HTML dashboard (d3 bundled in vendor/)
```

## Known limitation

SmartAdaptive never interrupts a running job, so on the convoy workload (B) Round Robin wins: 3.5 average wait against 40.8. Letting the learner also choose preemption is the planned next step.
