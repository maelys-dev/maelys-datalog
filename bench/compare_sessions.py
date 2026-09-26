#!/usr/bin/env python3
# SPDX-License-Identifier: MPL-2.0
"""Public solve_edb latency across revisions; no subtraction-based attribution."""
import csv
import json
import math
from pathlib import Path
import statistics
import sys
from compare_runs import METRICS, PASSES, compare, display, print_host

KEY = ("policy", "order", "values", "size")
CASES = {(p, o, v, s) for p in ("inert", "derive")
         for o in ("sorted", "reverse", "permuted", "duplicate", "strided")
         for v in ("integer", "symbol") for s in ("8", "16", "31", "32", "33", "64", "128", "256", "402", "maximum")}


def load(path, profile, commit):
    rows = {}
    with path.open(newline="") as stream:
        for row in csv.DictReader(stream):
            key = tuple(row[k] for k in KEY)
            if key not in CASES or key in rows:
                raise ValueError(f"unexpected/duplicate case: {path}/{key}")
            if (row["profile"], row["commit"], row["opt_level"], row["samples"]) != (profile, commit, "-O2", "301"):
                raise ValueError(f"wrong profile/revision/optimization/samples: {path}/{key}")
            values = [float(row[m]) for m in METRICS]
            if any(not math.isfinite(v) or v <= 0 for v in values) or values != sorted(values):
                raise ValueError(f"invalid timings: {path}/{key}")
            limit, entries = int(row["edb_limit"]), int(row["entries"])
            if entries != (limit if key[3] == "maximum" else int(key[3])) or not 0 < entries <= limit:
                raise ValueError(f"invalid capacity: {path}/{key}")
            if len(row["result_digest"]) != 16 or any(c not in "0123456789abcdef" for c in row["result_digest"]):
                raise ValueError(f"invalid result digest: {path}/{key}")
            rows[key] = row
    if rows.keys() != CASES:
        raise ValueError(f"incomplete case inventory: {path}")
    return rows


def report(directory):
    metadata = json.loads((directory / "metadata.json").read_text())
    print("# Public session solve comparison\n")
    print(f"Base: `{metadata['base']}`; head: `{metadata['head']}`.\n")
    print_host(metadata.get("host"))
    print(metadata.get("comparison_protocol", "Two A/A pairs precede A B A B.") + "\n")
    print("Same public harness, Clang -O2, SMALL/LARGE. "
          "50 warmups + 301 samples per case/pass. Time only solve_edb, including clock overhead; "
          "exclude compilation, session creation, input append, queries and result release. "
          "EDB membership/boundary absence and enumerated IDB values are checked outside timing; digests include "
          "canonical symbol IDs and must agree across revisions and input permutations.\n")
    print("Inert policy: out(X) :- p00(X), p31(X), with p31 empty. Deriving policy: out(X) :- p00(X). "
          "The inert run includes canonicalization, reset, solver setup and an empty join. It does "
          "NOT isolate materialization; subtracting its timing does not measure pure derivation. "
          "These fixtures do not characterize arbitrary recursive or aggregate policies.\n")
    print("Entries are spread across the minimum number of predicates needed for the runtime "
          "per-predicate bound; maximum is the runtime global EDB bound. Duplicate controls contain "
          "one distinct fact; strided values are spaced by 4096 (not a forced-collision case).\n")
    print("Below 10 µs (median of A/A medians), use minima; otherwise median and p95 with separate "
          "A/A floors. Aggregate p95 is the median of pass p95s, not a pooled percentile. "
          "Observed floors are not confidence intervals; sub-floor effects are indéterminé. "
          "A/A repeatability does not bound systematic placement effects between binaries. "
          "Above-floor rows alone do not establish a cause; no universal 10% tolerance applies.\n")
    total = indeterminate = 0
    for profile in ("SMALL", "LARGE"):
        runs = {p: load(directory / f"{profile}-sessions-{p}.csv", profile,
                        metadata["head"] if p.startswith("ab-B") else metadata["base"]) for p in PASSES}
        flags = {(r["compiler"], r["cflags"]) for rows in runs.values() for r in rows.values()}
        if len(flags) != 1 or not all(next(iter(flags))):
            raise ValueError(f"compiler/flags differ or missing: {profile}")
        if len({r["edb_limit"] for rows in runs.values() for r in rows.values()}) != 1:
            raise ValueError(f"capacity drift: {profile}")
        print(f"## {profile}\n\n| Case | Entries | Metric | A µs | B µs | B/A | A/A floor | Verdict |")
        print("| --- | ---: | --- | ---: | ---: | ---: | ---: | --- |")
        for key in sorted(CASES):
            if len({rows[key]["result_digest"] for rows in runs.values()}) != 1:
                raise ValueError(f"result mismatch across revisions: {profile}/{key}")
            if key[1] in ("sorted", "reverse", "permuted"):
                for rows in runs.values():
                    if rows[key]["result_digest"] != rows[(key[0], "sorted", key[2], key[3])]["result_digest"]:
                        raise ValueError(f"result mismatch across permutations: {profile}/{key}")
            aa = [runs[f"aa-{i}"][key] for i in range(1, 5)]
            a = [runs[f"ab-A{i}"][key] for i in (1, 2)]
            b = [runs[f"ab-B{i}"][key] for i in (1, 2)]
            micro = statistics.median(float(r["median_us"]) for r in aa) < 10
            for metric in ("min_us",) if micro else ("median_us", "p95_us"):
                baseline, changed, ratio, floor, verdict = compare(aa, a, b, metric)
                total += 1
                indeterminate += verdict.startswith("indéterminé")
                print(f"| {' / '.join(key)} | {aa[0]['entries']} | {metric} | {baseline:.6f} | {changed:.6f} | "
                      f"{display(ratio)} | {display(floor, True)} | {verdict} |")
        print()
    print(f"{indeterminate}/{total} comparisons are indéterminé. No unqualified speed or regression claim follows from these rows.")


if __name__ == "__main__":
    try:
        if len(sys.argv) != 2:
            raise ValueError("usage: compare_sessions.py OUTPUT")
        report(Path(sys.argv[1]))
    except (ValueError, OSError, KeyError) as error:
        raise SystemExit(f"Invalid or incomplete session evidence: {error}") from error
