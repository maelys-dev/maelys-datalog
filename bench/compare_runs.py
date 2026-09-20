#!/usr/bin/env python3
# SPDX-License-Identifier: MPL-2.0
"""Observed noise floors, not confidence bounds or automatic acceptance."""
import csv
import hashlib
import json
import math
from pathlib import Path
import statistics
import subprocess
import sys

SOLVER_KEY = ("benchmark", "group", "mode", "size", "selectivity")
INPUT_KEY = ("scenario", "capacity", "entries", "distinct_strings", "mode", "text_capacity")
PASSES = ("aa-1", "aa-2", "aa-3", "aa-4", "ab-A1", "ab-B1", "ab-A2", "ab-B2")
METRICS = ("min_us", "median_us", "p95_us")


def load(path, kind):
    rows = {}
    key_fields = SOLVER_KEY if kind == "solver" else INPUT_KEY
    metrics = METRICS + (tuple("clear_" + m for m in METRICS) if kind == "input" else ())
    with path.open(newline="") as stream:
        reader = csv.DictReader(stream)
        required = set(key_fields + metrics + ("samples",))
        if kind == "input":
            required.add("reserved_bytes")
        if not required.issubset(reader.fieldnames or ()):
            raise ValueError(f"missing CSV fields in {path}: {sorted(required - set(reader.fieldnames or ()))}")
        for row in reader:
            key = tuple(row[k] for k in key_fields)
            if key in rows or not all(key):
                raise ValueError(f"duplicate/empty case in {path}: {key}")
            expected = 1000 if kind == "solver" else 301
            if int(row["samples"]) != expected:
                raise ValueError(f"expected {expected} samples in {path}: {key}")
            for metric in metrics:
                value = float(row[metric])
                if not math.isfinite(value) or value < 0 or (value == 0 and not metric.startswith("clear_")):
                    raise ValueError(f"invalid {metric} in {path}: {key}")
            for prefix in ("", "clear_") if kind == "input" else ("",):
                if not (float(row[prefix + "min_us"]) <= float(row[prefix + "median_us"]) <= float(row[prefix + "p95_us"])):
                    raise ValueError(f"unordered timing statistics in {path}: {key}")
            if kind == "input" and int(row["reserved_bytes"]) <= 0:
                raise ValueError(f"invalid reserved_bytes in {path}: {key}")
            rows[key] = row
    if not rows:
        raise ValueError(f"empty benchmark: {path}")
    return rows


def compare(aa, a, b, metric):
    """Use a symmetric floor; retain the directional B/A ratio for readers."""
    values = [float(row[metric]) for row in aa + a + b]
    aggregate = min if metric.endswith("min_us") else statistics.median
    baseline = aggregate(values[4:6])
    changed = aggregate(values[6:8])
    if any(value == 0 for value in values):
        return baseline, changed, None, None, "indéterminé (clock resolution)"
    floor = max(max(x / y, y / x) - 1 for x, y in ((values[0], values[1]), (values[2], values[3])))
    ratio = changed / baseline
    deviation = max(ratio, 1 / ratio) - 1
    if deviation <= floor:
        verdict = "indéterminé"
    else:
        verdict = "slower" if ratio > 1 else "faster"
    return baseline, changed, ratio, floor, verdict


def display(value, percent=False):
    return "n/a" if value is None else (f"{value:.2%}" if percent else f"{value:.4f}")


def report(directory):
    metadata = json.loads((directory / "metadata.json").read_text())
    print("# Revision comparison\n")
    print(f"Base: `{metadata['base']}`; head: `{metadata['head']}`.\n")
    print("Clang -O2, SMALL/LARGE, complete solver and input matrices. No priority, "
          "affinity or case-selection adjustment. Each engine object compiled once "
          "per revision/profile, shared by both harnesses; no build during timing.\n")
    print("Two A/A pairs precede A B A B. Per metric, the observed floor is "
          "max(max(A1/A2,A2/A1)-1, max(A3/A4,A4/A3)-1). It is NOT a confidence "
          "interval. Below 10 µs (median of A/A medians), use the minimum of pass "
          "minima. Otherwise report the median of pass medians AND the median of "
          "pass p95s, with separate noise floors. The latter is not a pooled p95.\n")
    print("A sub-floor effect is **indéterminé**, never zero or a win. If hosted-runner "
          "noise masks the effect sought, use a dedicated machine; do not reinterpret "
          "the statistic, discard cases, or tune the threshold from this run.\n")
    if metadata["input_implementation_identical"]:
        print("**Input implementations are byte-identical. This run cannot establish "
              "a linear/indexed crossover or choose S.**\n")
    else:
        print("Input results identify revisions A/B, not inferred algorithm names. "
              "Verify A is linear and B indexed before using crossover rows for S; "
              "the workflow never writes a threshold into production.\n")
    indeterminate = total = 0
    for profile in ("SMALL", "LARGE"):
        for kind in ("solver", "input"):
            runs = {p: load(directory / f"{profile}-{kind}-{p}.csv", kind) for p in PASSES}
            keys = runs["aa-1"].keys()
            if any(rows.keys() != keys for rows in runs.values()):
                raise ValueError(f"case inventory mismatch: {profile}/{kind}")
            if kind == "solver":
                compiler_flags = set()
                for name, rows in runs.items():
                    expected = metadata["head"] if name.startswith("ab-B") else metadata["base"]
                    for row in rows.values():
                        if row.get("commit") != expected or row.get("opt_level") != "-O2":
                            raise ValueError(f"wrong revision/optimization: {profile}/{name}")
                        compiler_flags.add((row["compiler"], row["cflags"]))
                if len(compiler_flags) != 1:
                    raise ValueError(f"compiler/flags differ: {profile}")
            print(f"## {profile} / {kind}\n")
            print("| Case | Metric | A µs | B µs | B/A | A/A floor | Verdict |")
            print("| --- | --- | ---: | ---: | ---: | ---: | --- |")
            for key in keys:
                aa = [runs[f"aa-{i}"][key] for i in range(1, 5)]
                a = [runs[f"ab-A{i}"][key] for i in (1, 2)]
                b = [runs[f"ab-B{i}"][key] for i in (1, 2)]
                label = " / ".join(key).replace("|", "\\|")
                for prefix in ("", "clear_") if kind == "input" else ("",):
                    micro = statistics.median(float(row[prefix + "median_us"]) for row in aa) < 10
                    metrics = (prefix + "min_us",) if micro else (prefix + "median_us", prefix + "p95_us")
                    for metric in metrics:
                        baseline, changed, ratio, floor, verdict = compare(aa, a, b, metric)
                        total += 1
                        indeterminate += verdict.startswith("indéterminé")
                        print(f"| {label} | {metric} | {baseline:.6f} | {changed:.6f} | "
                              f"{display(ratio)} | {display(floor, True)} | {verdict} |")
            print()
            if kind == "input":
                print("| Input case | Reserved bytes A | Reserved bytes B |")
                print("| --- | ---: | ---: |")
                for key in keys:
                    # Capacity is deterministic; a mismatch across passes is a
                    # broken artifact, not a result to average.
                    ar = {int(runs[p][key]["reserved_bytes"]) for p in PASSES if not p.startswith("ab-B")}
                    br = {int(runs[p][key]["reserved_bytes"]) for p in ("ab-B1", "ab-B2")}
                    if len(ar) != 1 or len(br) != 1:
                        raise ValueError(f"storage drift: {profile}/{key}")
                    label = " / ".join(key).replace("|", "\\|")
                    print(f"| {label} | {ar.pop()} | {br.pop()} |")
                print()
    print(f"## Resolution limit\n\n{indeterminate}/{total} metric comparisons are indéterminé.")
    if indeterminate:
        print("For these cases, hosted-runner noise or clock resolution prevents "
              "resolving the observed effect. A dedicated machine is the next step "
              "if the decision depends on it; no gain or absence of regression is established.")
    print("\nInput clear timings include two clock reads around clearing a populated "
          "EDB; they are not clock-overhead-subtracted. Raw append/clear samples are "
          "in *.samples.csv. Setup, reporting and clear are outside append timing. "
          "Crossover cases pack five string positions per fact, so the capacity "
          "bound is at most four above the distinct-string count. Repetitive controls "
          "retain default capacity. Budget summaries (16/128/default/maximum text bytes) "
          "are in commands.log; default and maximum currently coincide.")


def metadata(output, base, head, repo):
    root = Path(__file__).resolve().parent
    def git(*args):
        return subprocess.check_output(["git", "-C", repo, *args], text=True).strip()
    source = "src/runtime/maelys_datalog_input_edb.c"
    a, b = (git("rev-parse", f"{revision}:{source}") for revision in (base, head))
    payload = {
        "base": base, "head": head, "driver_commit": git("rev-parse", "HEAD"),
        "driver_dirty": bool(git("status", "--porcelain")),
        "input_source_blobs": {"base": a, "head": b},
        "input_implementation_identical": a == b,
        "harness_sha256": {name: hashlib.sha256((root / name).read_bytes()).hexdigest()
                           for name in ("bench_datalog.c", "bench_input_edb.c", "bench_explanations.c",
                                        "compare_runs.py", "compare_explanations.py", "compare_revisions.sh", "Makefile.compare")},
    }
    (Path(output) / "metadata.json").write_text(json.dumps(payload, indent=2) + "\n")


if __name__ == "__main__":
    try:
        if len(sys.argv) == 6 and sys.argv[1] == "metadata":
            metadata(*sys.argv[2:])
        elif len(sys.argv) == 2:
            report(Path(sys.argv[1]))
        else:
            raise ValueError("usage: compare_runs.py OUTPUT | metadata OUTPUT BASE HEAD REPO")
    except (ValueError, OSError, KeyError, subprocess.CalledProcessError) as error:
        raise SystemExit(f"Invalid or incomplete benchmark evidence: {error}") from error
