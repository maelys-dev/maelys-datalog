#!/usr/bin/env python3
# SPDX-License-Identifier: MPL-2.0
"""Keep A/A observations; use a named layout control only to prioritize counts."""
import contextlib
import csv
import hashlib
import io
import json
from pathlib import Path
import statistics
import subprocess
import sys
from compare_runs import compare, print_host
from compare_sessions import CASES, KEY, PASSES, load, report as session_report
from report_solver_layout import functions, instructions

REFERENCE = .0994
REFERENCE_RUN = "https://github.com/maelys-dev/maelys-datalog/actions/runs/35579107425"


def residuals(root):
    # Validate complete inventory, provenance, flags, capacities and all oracles.
    with contextlib.redirect_stdout(io.StringIO()):
        session_report(root)
    meta = json.loads((root / "metadata.json").read_text())
    rows = []
    for profile in ("SMALL", "LARGE"):
        runs = {p: load(root / f"{profile}-sessions-{p}.csv", profile,
                        meta["head"] if p.startswith("ab-B") else meta["base"]) for p in PASSES}
        for key in sorted(CASES):
            aa = [runs[f"aa-{p}"][key] for p in (1, 2, 3, 4)]
            a = [runs[f"ab-A{p}"][key] for p in (1, 2)]
            b = [runs[f"ab-B{p}"][key] for p in (1, 2)]
            micro = statistics.median(float(r["median_us"]) for r in aa) < 10
            for metric in ("min_us",) if micro else ("median_us", "p95_us"):
                av, bv, ratio, floor, verdict = compare(aa, a, b, metric)
                if verdict == "slower":
                    rows.append(dict(profile=profile, case=list(key), metric=metric,
                                     baseline_us=av, candidate_us=bv, ratio=ratio,
                                     aa_floor=floor, above_reference=ratio > 1 + REFERENCE,
                                     result_digest=a[0]["result_digest"]))
    return meta, sorted(rows, key=lambda r: (-r["ratio"], r["profile"], r["case"], r["metric"]))


def verify_count(prefix, profile, revision, key, digest):
    with prefix.with_suffix(".csv").open(newline="") as stream:
        rows = list(csv.DictReader(stream))
    if len(rows) != 1:
        raise ValueError("counted case inventory")
    row = rows[0]
    if (tuple(row[k] for k in KEY), row["profile"], row["commit"], row["samples"], row["result_digest"]) != (
            tuple(key), profile, revision, "1", digest):
        raise ValueError("counted case provenance/oracle mismatch")
    if any(row[m] != "0.000000" for m in ("min_us", "median_us", "p95_us")):
        raise ValueError("counted run must not measure time")
    with prefix.with_suffix(".samples.csv").open(newline="") as stream:
        raw = list(csv.DictReader(stream))
    expected = dict(zip(KEY, key), sample="0", elapsed_us="0.000000")
    if raw != [expected]:
        raise ValueError("counted raw oracle inventory")


def run(root, workspace):
    meta, rows = residuals(root)
    output = root / "session-counts"
    output.mkdir()
    selection = {}
    for row in rows:
        if row["above_reference"]:
            selection[row["profile"], tuple(row["case"])] = row["result_digest"]
    (output / "selection.json").write_text(json.dumps(dict(
        reference=REFERENCE, reference_run=REFERENCE_RUN, residuals=rows,
        counted_cases=[dict(profile=p, case=k) for p, k in sorted(selection)]), indent=2) + "\n")
    print("# Session residuals and scoped instruction counts\n")
    print(f"Base `{meta['base']}`; candidate `{meta['head']}`.\n")
    print_host(meta.get("host"))
    print(f"The 9.94% reference comes from [one earlier solver layout control]({REFERENCE_RUN}). "
          "It is a triage reference, not a bound or a noise floor for sessions. "
          "Keep all A/A classifications; smaller signals remain visible. Only slower "
          "session rows above this named reference select a case for Callgrind. "
          "This does not apply that solver measurement as a universal acceptance threshold.\n")
    print("| Profile / case | Metric | A µs | B µs | Change | A/A floor | Above reference |\n"
          "| --- | --- | ---: | ---: | ---: | ---: | --- |")
    for r in rows:
        print(f"| {r['profile']} / {' / '.join(r['case'])} | {r['metric']} | "
              f"{r['baseline_us']:.6f} | {r['candidate_us']:.6f} | {r['ratio']-1:+.2%} | "
              f"{r['aa_floor']:.2%} | {'yes' if r['above_reference'] else 'no'} |")
    print(f"\n{len(rows)} slower metric rows; {len(selection)} distinct cases selected for counts.\n")
    if not selection:
        return
    print("The count driver reuses the session fixture/oracle with a separate compile-time mode. "
          "Its objects are the exact archived engine objects built before timing. It runs only "
          "the selected case: 50 uncounted warmups, then one counted solve_edb, twice per revision. "
          "Setup, clocks, oracle checks and release are excluded. This isolated driver changes "
          "binary layout and prior case history; no timing is inferred from its runs.\n")
    (output / "valgrind-version.txt").write_text(subprocess.check_output(["valgrind", "--version"], text=True))
    for ordinal, ((profile, key), digest) in enumerate(sorted(selection.items())):
        print(f"## {profile} / {' / '.join(key)}\n")
        values, costs = {}, {}
        for role in ("A", "B"):
            revision = meta["base" if role == "A" else "head"]
            binary = workspace / f"bin-{role}-{profile}" / "sessions-counts"
            (output / f"{role}-{profile}.sha256").write_text(hashlib.sha256(binary.read_bytes()).hexdigest() + "\n")
            for repeat in (1, 2):
                prefix = output / f"{ordinal:03d}-{profile}-{role}-{repeat}"
                with prefix.with_suffix(".log").open("w") as log:
                    subprocess.run(["valgrind", "--tool=callgrind", "--collect-atstart=no",
                                    "--error-exitcode=3", f"--callgrind-out-file={prefix}.out", str(binary),
                                    f"{prefix}.csv", f"{prefix}.samples.csv", *key],
                                   stdout=log, stderr=subprocess.STDOUT, check=True, timeout=300)
                verify_count(prefix, profile, revision, key, digest)
                values[role, repeat] = instructions(prefix.with_suffix(".out"))
                annotation = prefix.with_suffix(".functions.txt")
                with annotation.open("w") as stream:
                    subprocess.run(["callgrind_annotate", "--auto=no", "--threshold=100", f"{prefix}.out"],
                                   stdout=stream, check=True, timeout=60)
                # Native insertion is part of solve_edb materialization here.
                costs[role, repeat] = functions(annotation, exclude_insertion=False)
        print("| Revision | Ir run 1 | Ir run 2 | Repeated function counts identical |\n| --- | ---: | ---: | --- |")
        for role in ("A", "B"):
            print(f"| {role} | {values[role, 1]} | {values[role, 2]} | {costs[role, 1] == costs[role, 2]} |")
        print(f"\nFirst-run Ir change: {values['B', 1]/values['A', 1]-1:+.2%}. "
              "Instruction volume does not determine cycle/cache cost.\n")
        a, b = costs["A", 1], costs["B", 1]
        print("| Changed function, exclusive Ir | A | B | B−A |\n| --- | ---: | ---: | ---: |")
        for name in sorted(a.keys() | b.keys()):
            if a.get(name, 0) != b.get(name, 0):
                print(f"| {name} | {a.get(name, 0)} | {b.get(name, 0)} | {b.get(name, 0)-a.get(name, 0):+d} |")
        print()


if __name__ == "__main__":
    try:
        if len(sys.argv) != 3:
            raise ValueError("usage: diagnose_sessions.py REPORT_DIRECTORY WORKSPACE")
        run(Path(sys.argv[1]), Path(sys.argv[2]))
    except (ValueError, OSError, KeyError, subprocess.SubprocessError) as error:
        raise SystemExit(f"Invalid or incomplete session diagnostic: {error}") from error
