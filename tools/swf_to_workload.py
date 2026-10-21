#!/usr/bin/env python3
"""Convert a real job log from the Parallel Workloads Archive (Standard
Workload Format, .swf or .swf.gz) into this project's workload CSV:

    job_id,user_name,arrival_time,total_pages,priority,urgent

No public trace of university print jobs exists (print logs contain personal
data and are not published), so we use the standard real-world dataset for
scheduling research: the Parallel Workloads Archive,
https://www.cs.huji.ac.il/labs/parallel/workload/ . Each SWF record is a real
job submitted by a real user to a shared machine and queued for service -
the same shape of problem as print jobs queued for a shared printer.

Default input: KTH SP2 (Swedish Royal Institute of Technology, IBM SP2,
Sep 1996 - Aug 1997, 28,476 jobs, 214 users), cleaned version 2.1.
Please cite: D. G. Feitelson, D. Tsafrir, D. Krakov, "Experience with using
the Parallel Workloads Archive", J. Parallel & Distributed Computing 74(10),
2014.

FIELD MAPPING (SWF field numbers are 1-based)
  job_id        <- field 1  Job Number (renumbered 1..N within the window)
  total_pages   <- field 4  Run Time: ceil(run_time / seconds_per_page),
                   clamped to [1, max_pages]. Default 300 s per page, so a
                   page stands for 5 minutes of service; run times are
                   heavy-tailed, as print job sizes are.
  arrival_time  <- field 2  Submit Time, made relative to the first job in
                   the window and rescaled so that the printer's offered
                   load (total pages / arrival span) equals --load. The
                   real arrival pattern (bursts, quiet periods) is kept;
                   only the time unit changes.
  priority      <- field 9  Requested Time: jobs are ranked by how much time
                   their user asked for and split into five equal groups.
                   The shortest requests (users expecting quick turnaround)
                   get priority 5, the longest get 1. Missing (-1) -> 3.
  user_name     <- field 12 User ID, prefixed with a role. The log has no
                   roles, so this is an explicit ASSUMPTION, fixed per user
                   so every job of one user keeps the same role:
                     user_id % 20 == 0      -> Dean     (~5% of users)
                     user_id % 20 in 1..5   -> Faculty  (~25% of users)
                     otherwise              -> Student  (~70% of users)
  urgent        <- 1 for Faculty jobs with priority 5, else 0.
Only jobs with status 1 (completed) and run time > 0 are used.

Usage (keep the downloaded log in its own folder, e.g. datasets/):
  python -I tools/swf_to_workload.py datasets/KTH-SP2-1996-2.1-cln.swf.gz \
         workloads/real_kth_sp2.csv --start 0 --count 200 --load 0.9
"""

import argparse
import gzip
import math
import sys

MAX_JOBS = 256          # matches MAX_JOBS in src/main.c


def read_swf(path):
    opener = gzip.open if path.endswith(".gz") else open
    jobs = []
    with opener(path, "rt", encoding="ascii", errors="replace") as fh:
        for line in fh:
            line = line.strip()
            if not line or line.startswith(";"):
                continue
            fields = line.split()
            if len(fields) < 18:
                continue
            try:
                vals = [int(float(f)) for f in fields[:18]]
            except ValueError:
                continue
            job = {
                "submit": vals[1],
                "run_time": vals[3],
                "requested": vals[8],
                "status": vals[10],
                "user": vals[11],
            }
            if job["status"] == 1 and job["run_time"] > 0:
                jobs.append(job)
    return jobs


def role_for(user_id):
    r = user_id % 20
    if r == 0:
        return "Dean"
    if 1 <= r <= 5:
        return "Faculty"
    return "Student"


def priorities_by_requested_time(window):
    """Shortest requested time -> 5, longest -> 1 (five equal-size groups)."""
    known = sorted((j["requested"], i) for i, j in enumerate(window) if j["requested"] > 0)
    prio = [3] * len(window)
    for rank, (_, i) in enumerate(known):
        group = rank * 5 // len(known)          # 0 (shortest) .. 4 (longest)
        prio[i] = 5 - group
    return prio


def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    ap.add_argument("swf", help="input .swf or .swf.gz file")
    ap.add_argument("out", help="output workload CSV")
    ap.add_argument("--start", type=int, default=0, help="index of first usable job (default 0)")
    ap.add_argument("--count", type=int, default=200, help=f"number of jobs (max {MAX_JOBS})")
    ap.add_argument("--load", type=float, default=0.9, help="target printer load, e.g. 0.9 = 90%% busy")
    ap.add_argument("--seconds-per-page", type=int, default=300)
    ap.add_argument("--max-pages", type=int, default=100)
    args = ap.parse_args()

    if not 1 <= args.count <= MAX_JOBS:
        sys.exit(f"--count must be between 1 and {MAX_JOBS}")
    if args.load <= 0:
        sys.exit("--load must be positive")

    jobs = read_swf(args.swf)
    window = jobs[args.start:args.start + args.count]
    if len(window) < 2:
        sys.exit(f"only {len(window)} usable job(s) in that window")

    pages = [min(args.max_pages, max(1, math.ceil(j["run_time"] / args.seconds_per_page)))
             for j in window]
    prio = priorities_by_requested_time(window)

    first = window[0]["submit"]
    span_sec = max(1, window[-1]["submit"] - first)
    # Choose ticks-per-second so that total_pages / arrival_span == load.
    ticks_per_sec = sum(pages) / (args.load * span_sec)

    rows = []
    for k, (j, p, pr) in enumerate(zip(window, pages, prio), start=1):
        role = role_for(j["user"])
        arrival = int(round((j["submit"] - first) * ticks_per_sec))
        urgent = 1 if role == "Faculty" and pr == 5 else 0
        rows.append((k, f"{role}{j['user']}", arrival, p, pr, urgent))

    roles = {"Dean": 0, "Faculty": 0, "Student": 0}
    for r in rows:
        roles[r[1].rstrip("0123456789")] += 1

    with open(args.out, "w", newline="\n") as out:
        out.write("# Real workload converted from the Parallel Workloads Archive (SWF)\n")
        out.write(f"# Source: {args.swf.replace(chr(92), '/').split('/')[-1]}, usable jobs "
                  f"{args.start}..{args.start + len(window) - 1}\n")
        out.write("# Cite: Feitelson, Tsafrir & Krakov, J. Parallel Distrib. Comput. 74(10), 2014\n")
        out.write(f"# Mapping: pages = ceil(run_time / {args.seconds_per_page}s) capped at {args.max_pages}; "
                  f"arrivals rescaled to load {args.load}\n")
        out.write("# Mapping: priority 5..1 = shortest..longest requested time (quintiles)\n")
        out.write("# Mapping: role by user_id % 20 (0 Dean, 1-5 Faculty, else Student); "
                  "urgent = Faculty with priority 5\n")
        out.write(f"# Jobs: {len(rows)} (Dean {roles['Dean']}, Faculty {roles['Faculty']}, "
                  f"Student {roles['Student']}), total pages {sum(pages)}\n")
        out.write("# job_id,user_name,arrival_time,total_pages,priority,urgent\n")
        for r in rows:
            out.write(",".join(str(v) for v in r) + "\n")

    print(f"Wrote {len(rows)} jobs to {args.out}: Dean {roles['Dean']}, Faculty {roles['Faculty']}, "
          f"Student {roles['Student']}, total pages {sum(pages)}, "
          f"time scale {ticks_per_sec:.5f} ticks/s")


if __name__ == "__main__":
    main()
