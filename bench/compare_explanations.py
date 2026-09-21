#!/usr/bin/env python3
# SPDX-License-Identifier: MPL-2.0
"""Same-revision session workspace comparison, separate from revision A/B."""
import csv
import json
import math
from pathlib import Path
import statistics
import sys

from compare_runs import METRICS, PASSES, compare, display, print_host

CASES = {(scenario, kind) for scenario in ("fresh-result", "alternating-query", "cache-hit")
         for kind in ("true", "false")}


def load(path, mode, profile, commit):
    rows = {}
    with path.open(newline="") as stream:
        for row in csv.DictReader(stream):
            key = row["scenario"], row["kind"]
            if key not in CASES or key in rows:
                raise ValueError(f"unexpected/duplicate case: {path}/{key}")
            if (row["mode"], row["profile"], row["commit"], row["opt_level"], row["samples"]) != (
                    mode, profile, commit, "-O2", "301"):
                raise ValueError(f"wrong mode/profile/revision/optimization/samples: {path}/{key}")
            values = [float(row[m]) for m in METRICS]
            if any(not math.isfinite(v) or v <= 0 for v in values) or values != sorted(values):
                raise ValueError(f"invalid timings: {path}/{key}")
            reserved = int(row["workspace_bytes"])
            if (mode == "legacy" and reserved != 0) or (mode == "workspace" and reserved <= 0):
                raise ValueError(f"invalid workspace budget: {path}/{key}")
            if len(row["text_digest"]) != 16 or any(c not in "0123456789abcdef" for c in row["text_digest"]):
                raise ValueError(f"invalid text digest: {path}/{key}")
            rows[key] = row
    if rows.keys() != CASES:
        raise ValueError(f"incomplete case inventory: {path}")
    return rows


def report(directory):
    metadata = json.loads((directory / "metadata.json").read_text())
    enabled = (directory / "explanations-enabled.txt").read_text().strip()
    if enabled not in ("0", "1"):
        raise ValueError("invalid explanation availability")
    print("# Session explanation workspace comparison\n")
    print(f"One candidate revision: `{metadata['head']}`. A = legacy; B = configured workspace.\n")
    print_host(metadata.get("host"))
    if enabled == "0":
        print("Skipped: the candidate has no session explanation workspace API. Solver/input comparison remains available.")
        return
    print("Same binary, Clang -O2, two legacy A/A pairs before legacy/workspace/legacy/workspace. "
          "50 warmups and 301 samples per case/pass. Timed operation: measure + write via direct-text API, "
          "including clock overhead, with preallocated output. Both modes are checked byte for byte "
          "against legacy text before timing and every measured output is checked after timing.\n")
    print("Fresh-result: solve/release outside timing, empty cache. Alternating-query: two typed symbol "
          "values alternate, forcing cache misses. Cache-hit: the same query repeats after warmup. "
          "Compilation, session creation, solve, result release and text checks are excluded; "
          "this is neither end-to-end latency nor an allocation audit.\n")
    print("Below 10 µs (median of A/A medians), use minima; otherwise report medians and p95s "
          "with separate A/A floors. p95 aggregation is a median of pass p95s, not a pooled percentile. "
          "Observed floors are not confidence intervals. Sub-floor effects are indéterminé; "
          "they do not bound systematic differences between configurations or binaries. "
          "Distinguish placement from runner variance before requesting dedicated hardware.\n")
    total = indeterminate = 0
    for profile in ("SMALL", "LARGE"):
        runs = {p: load(directory / f"{profile}-explanations-{p}.csv",
                        "workspace" if p.startswith("ab-B") else "legacy", profile, metadata["head"])
                for p in PASSES}
        flags = {(row["compiler"], row["cflags"]) for rows in runs.values() for row in rows.values()}
        if len(flags) != 1 or not all(next(iter(flags))):
            raise ValueError(f"compiler/flags differ or missing: {profile}")
        budgets = {runs[p][key]["workspace_bytes"] for p in ("ab-B1", "ab-B2") for key in CASES}
        if len(budgets) != 1:
            raise ValueError(f"workspace budget drift: {profile}")
        print(f"## {profile}\n\nAdditional session workspace: A = 0 bytes; B = {budgets.pop()} bytes "
              "(maximum TRUE/FALSE bound, not total session/process memory).\n")
        print("| Case | Kind | Metric | Legacy µs | Workspace µs | B/A | A/A floor | Verdict |")
        print("| --- | --- | --- | ---: | ---: | ---: | ---: | --- |")
        for key in sorted(CASES):
            if len({rows[key]["text_digest"] for rows in runs.values()}) != 1:
                raise ValueError(f"text mismatch across passes: {profile}/{key}")
            aa = [runs[f"aa-{i}"][key] for i in range(1, 5)]
            a = [runs[f"ab-A{i}"][key] for i in (1, 2)]
            b = [runs[f"ab-B{i}"][key] for i in (1, 2)]
            micro = statistics.median(float(row["median_us"]) for row in aa) < 10
            for metric in ("min_us",) if micro else ("median_us", "p95_us"):
                baseline, changed, ratio, floor, verdict = compare(aa, a, b, metric)
                total += 1
                indeterminate += verdict.startswith("indéterminé")
                print(f"| {key[0]} | {key[1]} | {metric} | {baseline:.6f} | {changed:.6f} | "
                      f"{display(ratio)} | {display(floor, True)} | {verdict} |")
        print()
    print(f"{indeterminate}/{total} comparisons are indéterminé. Results concern this fixed small "
          "negation fixture; they do not establish gains for arbitrary policies or search truncation.")


if __name__ == "__main__":
    try:
        if len(sys.argv) != 2:
            raise ValueError("usage: compare_explanations.py OUTPUT")
        report(Path(sys.argv[1]))
    except (ValueError, OSError, KeyError) as error:
        raise SystemExit(f"Invalid or incomplete explanation evidence: {error}") from error
