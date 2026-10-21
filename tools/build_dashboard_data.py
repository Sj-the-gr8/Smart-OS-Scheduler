#!/usr/bin/env python3
# Sanchit Jindal
"""Build the data for the local dashboard (dashboard/index.html) from the
console output of smartprint.

`make dashboard` runs ./build/smartprint on every workload and saves each
run's output as <runs_dir>/run_<workload>.txt; this script parses those
files and writes, into <out_dir>:

  data.js               window.SQ_DATA = {...}, loaded by index.html
                        (a .js file, because browsers block fetch() of
                        local files opened with file://)
  policy_results.csv    one row per workload x policy
  learned_settings.csv  one row per workload: SmartAdaptive's learning

Score ("objective") is read from the Score column smartprint prints in its
comparison table (see metrics.h): the same measure SmartAdaptive optimises,
    priority-weighted average wait + tau_fair * (starved jobs / n)
with tau_fair = the program's reporting threshold (2 x average pages),
identical for every policy. Output from an older build without that column
is still accepted; the score is then recomputed from the job tables.

Usage: python3 -I tools/build_dashboard_data.py dashboard/runs dashboard
"""

import datetime
import glob
import json
import os
import re
import sys

# Short code and readable name per workload file; unknown workloads still
# appear, using their file name.
LABELS = {
    "test_a_monotonic": ("A", "Monotonic sizes"),
    "test_b_convoy": ("B", "Convoy"),
    "test_c_priorities": ("C", "Mixed priorities"),
    "test_d_simultaneous": ("D", "Simultaneous burst"),
    "test_e_short_inflow": ("E", "Short-job stream"),
    "test_f_batch": ("F", "32-job batch"),
    "test_g_multilevel": ("G", "Three-level mix"),
    "test_h_contention": ("H", "Contention bursts"),
    "real_kth_sp2": ("KTH", "Real KTH log"),
}


def grab(pattern, text, cast=float):
    m = re.search(pattern, text)
    if m is None:
        raise ValueError(f"pattern not found in run output: {pattern}")
    return cast(m.group(1))


def parse_run(workload, text, order):
    code, name = LABELS.get(workload, (workload, workload))
    tau = grab(r"tau_fair=(\d+)", text, int)
    jobs = grab(r"Loaded (\d+) job", text, int)

    results = []
    # Each policy section: "--- Name ---", its job table, then its metrics row.
    for m in re.finditer(r"^--- (\S+) ---\n(.*?)\n(Policy[^\n]*\n[^\n]*)$", text, re.S | re.M):
        policy, table, metrics = m.group(1), m.group(2), m.group(3)
        weighted = weights = 0.0
        n = starved = max_wait = 0
        for line in table.splitlines():
            f = line.split()
            if not f or not f[0].isdigit():
                continue
            prio, wait = int(f[4]), int(f[7])
            weighted += prio * wait
            weights += prio
            n += 1
            starved += wait > tau
            max_wait = max(max_wait, wait)
        row = metrics.splitlines()[1].split()
        if len(row) >= 10:
            # Columns printed by smartprint: ... Starved MaxWait PrioWait Score
            max_wait, prio_wait, objective = int(row[7]), float(row[8]), float(row[9])
        else:
            # Output from an older build without the score columns.
            prio_wait = round(weighted / weights, 2) if weights else 0.0
            objective = round(prio_wait + tau * starved / n, 2) if n else 0.0
        results.append({
            "workload": workload, "order": order, "code": code, "workload_name": name,
            "policy": policy,
            "avg_wait": float(row[1]), "avg_turnaround": float(row[2]),
            "avg_response": float(row[3]), "starved": int(row[6]),
            "prio_wait": prio_wait, "max_wait": max_wait,
            "objective": objective,
            "jobs": jobs,
        })

    learned = {
        "workload": workload, "order": order, "code": code, "workload_name": name, "jobs": jobs,
        "size_signal": grab(r"size signal\s+([\d.]+)", text),
        "priority_signal": grab(r"priority signal\s+([\d.]+)", text),
        "starvation_signal": grab(r"starvation signal\s+([\d.]+)", text),
        "alpha": grab(r"Learned: alpha=([\d.]+)", text),
        "beta": grab(r"Learned: .*? beta=([\d.]+)", text),
        "gamma": grab(r"Learned: .*? gamma=([\d.]+)", text),
        "tau_override": grab(r"tau_override=(\d+)  objective", text, int),
        "tau_fixed": tau,
        "objective_fixed": grab(r"objective [\d.]+ \(fixed: ([\d.]+)\)", text),
        "objective_learned": grab(r"objective ([\d.]+) \(fixed", text),
    }
    return results, learned


def write_csv(path, rows):
    cols = list(rows[0])
    with open(path, "w", newline="\n", encoding="utf-8") as fh:
        fh.write(",".join(cols) + "\n")
        for r in rows:
            fh.write(",".join(str(r[c]) for c in cols) + "\n")


def main():
    if len(sys.argv) != 3:
        sys.exit(__doc__.strip().splitlines()[-1])
    runs_dir, out_dir = sys.argv[1], sys.argv[2]

    files = glob.glob(os.path.join(runs_dir, "run_*.txt"))
    names = [os.path.basename(f)[4:-4] for f in files]
    known = [w for w in LABELS if w in names]
    ordered = known + sorted(w for w in names if w not in LABELS)
    if not ordered:
        sys.exit(f"no run_*.txt files in {runs_dir}; run 'make dashboard'")

    results, learning = [], []
    for i, w in enumerate(ordered, start=1):
        with open(os.path.join(runs_dir, f"run_{w}.txt"), encoding="utf-8") as fh:
            r, l = parse_run(w, fh.read(), i)
        results.extend(r)
        learning.append(l)

    data = {
        "generated": datetime.datetime.now().strftime("%Y-%m-%d %H:%M"),
        "results": results,
        "learning": learning,
    }
    with open(os.path.join(out_dir, "data.js"), "w", newline="\n", encoding="utf-8") as fh:
        fh.write("/* Generated by tools/build_dashboard_data.py - do not edit by hand. */\n")
        fh.write("window.SQ_DATA = " + json.dumps(data, indent=1) + ";\n")
    write_csv(os.path.join(out_dir, "policy_results.csv"), results)
    write_csv(os.path.join(out_dir, "learned_settings.csv"), learning)
    print(f"Dashboard data: {len(ordered)} workloads, {len(results)} policy rows -> {out_dir}/data.js")


if __name__ == "__main__":
    main()
