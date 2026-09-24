#!/usr/bin/env python3
"""Proof-only exhaustive session counts and neutral text-placement controls."""
import collections
import csv
import gzip
import hashlib
import json
from pathlib import Path
import re
import statistics
import subprocess
import sys

from compare_runs import compare, display
from compare_sessions import CASES, KEY, load
from diagnose_sessions import CACHE_FLAGS, CONTROLS, events

PADS = (0, 16, 64, 256)
EVENTS = ("Ir", "Dr", "Dw", "Bcm", "I1mr")
STRICT_EVENTS = EVENTS[:3]
TARGET = "solve_once_derive_ordered"
PREPARED = "maelys_datalog_prepared_session_solve_materialized_ex"
FIELD = r"\s*([\d,]+|\.)\s+(?:\([\d. ]+%\)\s+)?"
ROW = re.compile("^" + FIELD * len(EVENTS) + r"(\S+):(.+?)(?:\s+\[.*\])?\s*$")


def annotated(text):
    result = {}
    for line in text.splitlines():
        match = ROW.match(line)
        if not match:
            continue
        name = re.sub(r"'\d+$", "", match[7])
        values = result.setdefault(name, dict.fromkeys(EVENTS, 0))
        for i, event in enumerate(EVENTS, 1):
            values[event] += 0 if match[i] == "." else int(match[i].replace(",", ""))
    if not result:
        raise ValueError("missing exclusive function counts")
    return result


def strict_verdict(a, b, repeats):
    if not repeats:
        return "REFUSE: nonidentical repeats"
    if any(b[e] > a[e] for e in STRICT_EVENTS):
        return "REFUSE: ordered derivation increased"
    return "PASS"


def run_timing(binary, prefix):
    subprocess.run([str(binary), str(prefix) + ".csv", str(prefix) + ".samples.csv"], check=True)


def symbol_map(binary):
    text = subprocess.check_output(["nm", "-n", "-S", str(binary)], text=True)
    wanted = ("maelys_datalog_edb_add_fact", TARGET)
    values = {}
    for name in wanted:
        found = re.findall(r"^([0-9a-f]+) ([0-9a-f]+) [tT] " + name + r"$", text, re.M)
        if len(found) != 1:
            raise ValueError("missing unique symbol " + name)
        values[name] = int(found[0][0], 16)
    return text, values


def record_binaries(root, workspace):
    hashes = {}
    codegen = []
    for profile in ("SMALL", "LARGE"):
        for role in ("A", "B"):
            origin = None
            for pad in PADS:
                binary = workspace / f"bin-{role}-{profile}/proof-session-{pad}"
                prefix = root / f"{profile}-{role}-pad-{pad}"
                hashes[binary.name + "-" + role + "-" + profile] = hashlib.sha256(binary.read_bytes()).hexdigest()
                text, addresses = symbol_map(binary)
                prefix.with_suffix(".nm").write_text(text)
                if pad == 0:
                    origin = addresses
                elif any(addresses[k] - origin[k] != pad for k in addresses):
                    raise ValueError(f"unexpected symbol displacement: {profile}/{role}/{pad}")
                with gzip.open(str(prefix) + ".asm.gz", "wb") as stream:
                    stream.write(subprocess.check_output(["objdump", "-d", str(binary)]))
                if pad == 0:
                    assembly = subprocess.check_output(["objdump", "-d", "--no-show-raw-insn", "--disassemble=" + TARGET, str(binary)], text=True)
                    (root / f"{profile}-{role}-ordered.asm").write_text(assembly)
                    subtractions = re.findall(r"sub\s+\$(0x[0-9a-f]+),%rsp", assembly)
                    codegen.append(dict(profile=profile, role=role, function=TARGET,
                                        instructions_including_nops=len(re.findall(r"^\s*[0-9a-f]+:\s+\S", assembly, re.M)),
                                        stack_subtractions=subtractions, symbol_address=addresses[TARGET]))
            origin = None
            for pad in PADS:
                binary = workspace / f"bin-{role}-{profile}/proof-counts-{pad}"
                hashes[f"counts-{role}-{profile}-{pad}"] = hashlib.sha256(binary.read_bytes()).hexdigest()
                text, addresses = symbol_map(binary)
                (root / f"counts-{profile}-{role}-pad-{pad}.nm").write_text(text)
                if pad == 0:
                    origin = addresses
                elif any(addresses[k] - origin[k] != pad for k in addresses):
                    raise ValueError("unexpected count-driver symbol displacement")
                with gzip.open(root / f"counts-{profile}-{role}-pad-{pad}.asm.gz", "wb") as stream:
                    stream.write(subprocess.check_output(["objdump", "-d", str(binary)]))
    (root / "binaries.sha256.json").write_text(json.dumps(hashes, indent=2) + "\n")
    (root / "codegen.json").write_text(json.dumps(codegen, indent=2) + "\n")


def placement(root, workspace, metadata):
    # ALL binaries/variants have already been built, before the general timings.
    for profile in ("SMALL", "LARGE"):
        for role in ("A", "B"):
            for repeat in range(1, 5):
                run_timing(workspace / f"bin-{role}-{profile}/proof-session-0",
                           root / f"{profile}-{role}-aa-{repeat}")
    for repeat in (1, 2):
        for pad in PADS:
            for profile in ("SMALL", "LARGE"):
                for role in ("A", "B"):
                    run_timing(workspace / f"bin-{role}-{profile}/proof-session-{pad}",
                               root / f"{profile}-{role}-pad-{pad}-{repeat}")
    report = ["# Full session matrix: neutral placement control", "",
              "A = PR base; B = corrected PR head. The unchanged timing fixture is compiled once per revision/profile; engine objects are shared with the general benchmark. Unreachable 0/16/64/256-byte text padding precedes the EDB object. Symbols must move by exactly the requested bytes. Every variant and pass is retained. No affinity or priority adjustment.", "",
              "Both A/A pairs of each unpadded revision/profile finish before two interleaved rounds of all layouts. Same 50 warmups and 301 samples per case. Oracles and canonical symbol IDs are checked. Minima below 10 microseconds; otherwise median and p95 with their own floors. This separate relinked driver is interpreted separately from the general benchmark.", "",
              "The per-case control band is the largest symmetric ratio between declared padding variants of the same revision. A residual inside it is labelled compatible with placement (the requested classification convention), not proof of causation. Above-band residuals remain unattributed. Original A/A classifications, including both B passes, remain in the general reports.", "",
              "| Profile | Case | Metric | B/A pad 0 | A/A floor | Classification | Control band | Placement classification |",
              "|---|---|---|---:|---:|---|---:|---|"]
    rows_out = []
    for profile in ("SMALL", "LARGE"):
        runs = {}
        for role in ("A", "B"):
            revision = metadata["base" if role == "A" else "head"]
            for repeat in range(1, 5):
                runs[role, "aa", repeat] = load(root / f"{profile}-{role}-aa-{repeat}.csv", profile, revision)
            for pad in PADS:
                for repeat in (1, 2):
                    runs[role, pad, repeat] = load(root / f"{profile}-{role}-pad-{pad}-{repeat}.csv", profile, revision)
        for key in sorted(CASES):
            if len({r[key]["result_digest"] for r in runs.values()}) != 1:
                raise ValueError("placement oracle drift")
            aa = [runs["A", "aa", p][key] for p in range(1, 5)]
            metrics = ("min_us",) if statistics.median(float(r["median_us"]) for r in aa) < 10 else ("median_us", "p95_us")
            for metric in metrics:
                per_layout = []
                aggregate = min if metric == "min_us" else statistics.median
                band = 0
                for role in ("A", "B"):
                    values = [aggregate(float(runs[role, pad, p][key][metric]) for p in (1, 2)) for pad in PADS]
                    band = max(band, max(values) / min(values) - 1)
                    for pad, value in zip(PADS, values):
                        ra = [runs[role, "aa", p][key] for p in range(1, 5)]
                        rv = compare(ra, [runs[role, 0, p][key] for p in (1, 2)],
                                     [runs[role, pad, p][key] for p in (1, 2)], metric)
                        per_layout.append(dict(role=role, pad=pad, value=value, ratio=rv[2], floor=rv[3], verdict=rv[4]))
                av, bv, ratio, floor, verdict = compare(aa, [runs["A", 0, p][key] for p in (1, 2)],
                                                       [runs["B", 0, p][key] for p in (1, 2)], metric)
                residual = max(ratio, 1 / ratio) - 1
                classification = "indeterminate" if verdict.startswith("indéterminé") else "compatible with placement (band convention)" if residual <= band else "unattributed (above control band)"
                report.append(f"| {profile} | {' / '.join(key)} | {metric} | {display(ratio)} | {display(floor, True)} | {verdict} | {band:.2%} | {classification} |")
                rows_out.append(dict(profile=profile, case=key, metric=metric, a=av, b=bv, ratio=ratio,
                                     floor=floor, verdict=verdict, control_band=band, classification=classification,
                                     variants=per_layout))
    (root / "placement.md").write_text("\n".join(report) + "\n")
    (root / "placement.json").write_text(json.dumps(rows_out, indent=2) + "\n")


def bulk_counts(root, workspace, metadata, general, pad=0):
    output = root / f"counts-pad-{pad}"
    output.mkdir()
    all_costs = {}
    for profile in ("SMALL", "LARGE"):
        for role in ("A", "B"):
            revision = metadata["base" if role == "A" else "head"]
            oracle_rows = list(csv.DictReader((root / f"oracle-{profile}-{role}.csv").open()))
            expected = {tuple(r[k] for k in KEY): r for r in oracle_rows}
            if len(oracle_rows) != 200 or expected.keys() != CASES:
                raise ValueError("incomplete native oracle inventory")
            binary = workspace / f"bin-{role}-{profile}/proof-counts-{pad}"
            for repeat in (1, 2):
                prefix = output / f"{profile}-{role}-{repeat}"
                with prefix.with_suffix(".log").open("w") as log:
                    subprocess.run(["valgrind", "--tool=callgrind", *CACHE_FLAGS, "--collect-atstart=no", "--error-exitcode=3",
                                    f"--callgrind-out-file={prefix}.out", str(binary), f"{prefix}.csv", f"{prefix}.samples.csv"],
                                   stdout=log, stderr=subprocess.STDOUT, check=True, timeout=3600)
                rows = list(csv.DictReader(prefix.with_suffix(".csv").open()))
                if len(rows) != len(CASES) or {tuple(r[k] for k in KEY) for r in rows} != CASES:
                    raise ValueError("incomplete counted case inventory")
                for row in rows:
                    key = tuple(row[k] for k in KEY)
                    if (row["commit"], row["profile"], row["samples"], row["result_digest"]) != (revision, profile, "1", expected[key]["result_digest"]):
                        raise ValueError("counted provenance/oracle drift")
                    if any(row[k] != "0.000000" for k in ("min_us", "median_us", "p95_us")):
                        raise ValueError("timing inside counted run")
                paths = list(output.glob(prefix.name + ".out.*"))
                if len(paths) != len(CASES):
                    raise ValueError(f"expected 200 labelled dumps, got {len(paths)}")
                seen = set()
                for path in paths:
                    text = path.read_text()
                    labels = re.findall(r"^desc: Trigger: Client Request: (.+)$", text, re.M)
                    if len(labels) != 1:
                        raise ValueError("missing unique dump label")
                    key = tuple(labels[0].split("/"))
                    if key not in CASES or key in seen:
                        raise ValueError("unexpected duplicate dump")
                    seen.add(key)
                    annotation = subprocess.check_output(["callgrind_annotate", "--auto=no", "--threshold=100",
                                                         "--show=" + ",".join(EVENTS), "--sort=Ir", str(path)], text=True)
                    Path(str(path) + ".functions.txt").write_text(annotation)
                    function_costs = annotated(annotation)
                    if key[0] == "derive" and TARGET not in function_costs:
                        raise ValueError("missing ordered derivation in deriving policy")
                    # Also verify that the complete event summary is well-formed.
                    totals = events(path)
                    remainder = {e: totals[e] - sum(v[e] for v in function_costs.values()) for e in EVENTS}
                    if any(remainder[e] < 0 for e in STRICT_EVENTS):
                        raise ValueError("exclusive annotation exceeds total Ir/Dr/Dw events")
                    # Callgrind can leave dump-boundary events unattributed.
                    # Preserve them explicitly and refuse any A/B increase.
                    function_costs["<unattributed-summary-remainder>"] = remainder
                    all_costs[profile, role, repeat, key] = function_costs
                print("Counted", profile, role, repeat, "pad", pad, "all 200 cases", flush=True)
    return all_costs


def count_report(root, costs):
    report = ["# Exhaustive session Callgrind acceptance", "",
              "All 400 cases (200 per profile), two separate processes per revision/profile. Each case has 50 uncounted warmups followed by exactly one counted solve_edb. Per-case dump and reset happen after collection is disabled. Setup, clocks, oracle and result release are excluded. Batch fixture order is identical on A/B; this count driver is separate from timing and its cache/history differs from the old single-case driver.", "",
              "Ir/Dr/Dw are exclusive software event counts, not CPU cycles or byte counts. Bcm and I1mr are reported separately and never used to relax the strict count criterion. Every one-unit increase in solve_once_derive_ordered is a refusal. Repeated Ir/Dr/Dw maps must agree. Any aggregate-path execution in this aggregate-free matrix is a refusal. The known prepared-session +8 Ir/+2 Dw is recorded explicitly, not silently waived; its treatment follows the user's clarification.", "",
              "| Profile | Case | A Ir | B Ir | Delta Ir | A Dr | B Dr | Delta Dr | A Dw | B Dw | Delta Dw | Repeats | Verdict |",
              "|---|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|---|---|"]
    table = []
    function_rows = []
    failures = []
    unexpected = []
    for profile in ("SMALL", "LARGE"):
        for key in sorted(CASES):
            a, b = (costs[profile, role, 1, key] for role in ("A", "B"))
            repeated = all({n: {e: v[e] for e in STRICT_EVENTS} for n, v in costs[profile, role, 1, key].items() if any(v[e] for e in STRICT_EVENTS)} ==
                           {n: {e: v[e] for e in STRICT_EVENTS} for n, v in costs[profile, role, 2, key].items() if any(v[e] for e in STRICT_EVENTS)} for role in ("A", "B"))
            target_a, target_b = a.get(TARGET, dict.fromkeys(EVENTS, 0)), b.get(TARGET, dict.fromkeys(EVENTS, 0))
            verdict = strict_verdict(target_a, target_b, repeated)
            if verdict != "PASS":
                failures.append(dict(profile=profile, case=key, reason=verdict))
            row = dict(profile=profile, case=key, repeat_identical=repeated, verdict=verdict)
            cells = []
            for event in EVENTS:
                row[event] = dict(a=target_a[event], b=target_b[event], delta=target_b[event] - target_a[event],
                                  a_repeat2=costs[profile, "A", 2, key].get(TARGET, dict.fromkeys(EVENTS, 0))[event],
                                  b_repeat2=costs[profile, "B", 2, key].get(TARGET, dict.fromkeys(EVENTS, 0))[event])
                if event in STRICT_EVENTS:
                    cells += [str(target_a[event]), str(target_b[event]), f"{row[event]['delta']:+d}"]
            report.append(f"| {profile} | {' / '.join(key)} | " + " | ".join(cells) + f" | {repeated} | {verdict} |")
            table.append(row)
            for name in sorted(a.keys() | b.keys()):
                av, bv = a.get(name, dict.fromkeys(EVENTS, 0)), b.get(name, dict.fromkeys(EVENTS, 0))
                delta = {e: bv[e] - av[e] for e in EVENTS}
                if any(delta.values()) or name == TARGET:
                    function_rows.append(dict(profile=profile, case=key, function=name, a=av, b=bv, delta=delta,
                                              a_repeat2=costs[profile, "A", 2, key].get(name, dict.fromkeys(EVENTS, 0)),
                                              b_repeat2=costs[profile, "B", 2, key].get(name, dict.fromkeys(EVENTS, 0))))
                if "aggregate" in name and (av["Ir"] or bv["Ir"]):
                    failures.append(dict(profile=profile, case=key, reason="aggregate function executed", function=name))
                if name != PREPARED and any(delta[e] > 0 for e in STRICT_EVENTS):
                    unexpected.append(dict(profile=profile, case=key, function=name, delta=delta))
                if name == PREPARED and any(delta[e] > limit for e, limit in {"Ir": 8, "Dr": 0, "Dw": 2}.items()):
                    unexpected.append(dict(profile=profile, case=key, function=name, delta=delta, reason="exceeds named +8 Ir/+0 Dr/+2 Dw exception"))
    report.extend(["", f"Strict ordered-derivation/absence failures: {len(failures)}. Additional increases outside the named prepared-session exception: {len(unexpected)}.", "",
                   "Every changed function (including all prepared-session deltas) and separate Bcm/I1mr observations are retained in functions.json. Raw per-function annotations and dumps are retained for every case and repeat."])
    (root / "counts.md").write_text("\n".join(report) + "\n")
    (root / "counts.json").write_text(json.dumps(table, indent=2) + "\n")
    (root / "functions.json").write_text(json.dumps(function_rows, indent=2) + "\n")
    (root / "acceptance.json").write_text(json.dumps(dict(strict_failures=failures, unexpected_increases=unexpected), indent=2) + "\n")
    cache_report = ["# Separate placement-sensitive events: ordered derivation", "",
                    "Bcm and I1mr are modelled events, not acceptance thresholds for Ir/Dr/Dw.", "",
                    "| Profile | Case | Event | A1 | A2 | B1 | B2 | B1-A1 |",
                    "|---|---|---|---:|---:|---:|---:|---:|"]
    for row in table:
        for event in ("Bcm", "I1mr"):
            values = row[event]
            cache_report.append(f"| {row['profile']} | {' / '.join(row['case'])} | {event} | {values['a']} | {values['a_repeat2']} | {values['b']} | {values['b_repeat2']} | {values['delta']:+d} |")
    (root / "placement-events.md").write_text("\n".join(cache_report) + "\n")
    return failures, unexpected


def separate_passes(root, general, metadata):
    from compare_runs import load as load_general
    report = ["# Every general A/B pass retained separately", "",
              "Same four A/A observations and metric selection as the aggregate reports. A1/B1 and A2/B2 are classified separately; no B1 or outlier is removed. Minima below 10 microseconds, otherwise median and p95. These classifications complement, not replace, the complete aggregate comparisons.", "",
              "| Profile | Matrix | Case | Pass | Metric | A us | B us | B/A | A/A floor | Verdict |",
              "|---|---|---|---|---|---:|---:|---:|---:|---|"]
    output = []
    for profile in ("SMALL", "LARGE"):
        for matrix in ("solver", "input", "sessions"):
            def get(p):
                path = general / f"{profile}-{matrix}-{p}.csv"
                if matrix == "sessions":
                    return load(path, profile, metadata["head"] if p.startswith("ab-B") else metadata["base"])
                return load_general(path, matrix)
            runs = {p: get(p) for p in ("aa-1", "aa-2", "aa-3", "aa-4", "ab-A1", "ab-B1", "ab-A2", "ab-B2")}
            for key in sorted(runs["aa-1"]):
                aa = [runs[f"aa-{p}"][key] for p in range(1, 5)]
                for prefix in ("", "clear_") if matrix == "input" else ("",):
                    micro = statistics.median(float(r[prefix + "median_us"]) for r in aa) < 10
                    for metric in ((prefix + "min_us",) if micro else (prefix + "median_us", prefix + "p95_us")):
                        for repeat in (1, 2):
                            a, b = runs[f"ab-A{repeat}"][key], runs[f"ab-B{repeat}"][key]
                            av, bv, ratio, floor, verdict = compare(aa, [a, a], [b, b], metric)
                            report.append(f"| {profile} | {matrix} | {' / '.join(key)} | A{repeat}/B{repeat} | {metric} | {av:.6f} | {bv:.6f} | {display(ratio)} | {display(floor, True)} | {verdict} |")
                            output.append(dict(profile=profile, matrix=matrix, case=key, repeat=repeat, metric=metric,
                                               a=av, b=bv, ratio=ratio, floor=floor, verdict=verdict))
    (root / "passes.md").write_text("\n".join(report) + "\n")
    (root / "passes.json").write_text(json.dumps(output, indent=2) + "\n")


def layout_count_check(root, base_costs, padded_costs, pad):
    rows, failures = [], []
    for (profile, role, repeat, key), baseline in base_costs.items():
        actual = padded_costs[profile, role, repeat, key]
        changed = []
        for name in baseline.keys() | actual.keys():
            a, b = baseline.get(name, dict.fromkeys(EVENTS, 0)), actual.get(name, dict.fromkeys(EVENTS, 0))
            delta = {e: b[e] - a[e] for e in EVENTS}
            if any(delta.values()):
                changed.append(dict(function=name, delta=delta))
            if any(delta[e] for e in STRICT_EVENTS):
                failures.append(dict(profile=profile, role=role, repeat=repeat, case=key, pad=pad, function=name, delta=delta))
        rows.append(dict(profile=profile, role=role, repeat=repeat, case=key, pad=pad, changed_functions=changed))
    (root / f"placement-counts-{pad}.json").write_text(json.dumps(dict(observations=rows, strict_drift=failures), indent=2) + "\n")
    return failures


def quality_and_controls(root, general, metadata):
    pass_names = ("aa-1", "aa-2", "aa-3", "aa-4", "ab-A1", "ab-B1", "ab-A2", "ab-B2")
    runs = {}
    quality = ["# Median/minimum quality check, all general session passes", "",
               "The directive's threshold is 1.2. All flagged cases and original samples are retained; no statistic or case selection is changed. A threshold crossing alone does not prove the cause of an anomaly.", "",
               "| Profile | Pass | Cases | Cases with median/min >= 1.2 | Maximum ratio |",
               "|---|---|---:|---:|---:|"]
    flagged = []
    for profile in ("SMALL", "LARGE"):
        oracle = {tuple(r[k] for k in KEY): r["result_digest"] for r in csv.DictReader((root / f"oracle-{profile}-A.csv").open())}
        for p in pass_names:
            revision = metadata["head"] if p.startswith("ab-B") else metadata["base"]
            data = load(general / f"{profile}-sessions-{p}.csv", profile, revision)
            runs[profile, p] = data
            if any(data[k]["result_digest"] != oracle[k] for k in CASES):
                raise ValueError("native timed/count-mode oracle drift")
            ratios = {k: float(r["median_us"]) / float(r["min_us"]) for k, r in data.items()}
            bad = [(k, v) for k, v in ratios.items() if v >= 1.2]
            quality.append(f"| {profile} | {p} | 200 | {len(bad)} | {max(ratios.values()):.6f} |")
            for key, value in bad:
                samples = [float(r["elapsed_us"]) for r in csv.DictReader((general / f"{profile}-sessions-{p}.samples.csv").open()) if tuple(r[k] for k in KEY) == key]
                assert len(samples) == 301
                flagged.append(dict(profile=profile, pass_name=p, case=key, median_min_ratio=value, samples=samples))
    quality += ["", "| Profile | Pass | Case | Median/min |", "|---|---|---|---:|"]
    for row in flagged:
        quality.append(f"| {row['profile']} | {row['pass_name']} | {' / '.join(row['case'])} | {row['median_min_ratio']:.6f} |")
    (root / "pass-quality.md").write_text("\n".join(quality) + "\n")
    (root / "pass-quality.json").write_text(json.dumps(flagged, indent=2) + "\n")
    controls = ["# Six predeclared session controls", "",
                "The general benchmark residual and A/A verdict remain unchanged. The control band comes from the same case under all declared neutral padding variants of each revision in the separate placement fixture. Inside-band residuals are labelled placement by the directive's operational convention; this does not by itself establish a causal explanation across different driver layouts. Above-band residuals remain unattributed. All underlying case/pass classifications remain available.", "",
                "| Profile | Case | Metric | General B/A | A/A floor | Original verdict | Control band | Residual classification |",
                "|---|---|---|---:|---:|---|---:|---|"]
    for profile, key in CONTROLS:
        aa = [runs[profile, f"aa-{p}"][key] for p in range(1, 5)]
        metrics = ("min_us",) if statistics.median(float(r["median_us"]) for r in aa) < 10 else ("median_us", "p95_us")
        for metric in metrics:
            av, bv, ratio, floor, verdict = compare(aa, [runs[profile, f"ab-A{p}"][key] for p in (1, 2)],
                                                   [runs[profile, f"ab-B{p}"][key] for p in (1, 2)], metric)
            aggregate = min if metric == "min_us" else statistics.median
            band = 0
            for role in ("A", "B"):
                revision = metadata["base" if role == "A" else "head"]
                values = [aggregate(float(load(root / f"{profile}-{role}-pad-{pad}-{p}.csv", profile, revision)[key][metric]) for p in (1, 2)) for pad in PADS]
                band = max(band, max(values) / min(values) - 1)
            deviation = max(ratio, 1 / ratio) - 1
            classification = "indeterminate" if verdict.startswith("indéterminé") else "placement (directive's band convention)" if deviation <= band else "unattributed (above band)"
            controls.append(f"| {profile} | {' / '.join(key)} | {metric} | {display(ratio)} | {display(floor, True)} | {verdict} | {band:.2%} | {classification} |")
    (root / "controls.md").write_text("\n".join(controls) + "\n")


def counts_phase(general, workspace):
    root = general / "session-proof"
    root.mkdir()
    metadata = json.loads((general / "metadata.json").read_text())
    driver = Path(__file__).parent
    hashes = {p.name: hashlib.sha256(p.read_bytes()).hexdigest() for p in
              (driver / "bench_sessions.c", driver / "bench_sessions_all_counts.c", driver / "session_proof.py", driver / "Makefile.sessions-proof")}
    (root / "protocol.json").write_text(json.dumps(dict(base=metadata["base"], head=metadata["head"], profiles=["SMALL", "LARGE"],
                                                       pads=PADS, cases_per_profile=200, counted_repeats=2, warmup=50,
                                                       cache_model=CACHE_FLAGS, harness_sha256=hashes), indent=2) + "\n")
    record_binaries(root, workspace)
    # Execute the count-only driver natively to check oracles before timing.
    # This mode contains no clock calls and produces exactly zero timing fields.
    for profile in ("SMALL", "LARGE"):
        oracles = {}
        for role in ("A", "B"):
            prefix = root / f"oracle-{profile}-{role}"
            run_timing(workspace / f"bin-{role}-{profile}/proof-counts-0", prefix)
            rows = list(csv.DictReader(prefix.with_suffix(".csv").open()))
            if len(rows) != 200 or {tuple(r[k] for k in KEY) for r in rows} != CASES:
                raise ValueError("native count-mode oracle inventory")
            for row in rows:
                if any(row[e] != "0.000000" for e in ("min_us", "median_us", "p95_us")) or row["samples"] != "1":
                    raise ValueError("native oracle unexpectedly measured time")
            oracles[role] = {tuple(r[k] for k in KEY): r["result_digest"] for r in rows}
        if oracles["A"] != oracles["B"]:
            raise ValueError("native A/B oracle drift before timing")
    costs = bulk_counts(root, workspace, metadata, general)
    failures, unexpected = count_report(root, costs)
    if failures or unexpected:
        raise SystemExit("Strict session proof refused; all raw evidence retained")
    drift = []
    for pad in PADS[1:]:
        padded = bulk_counts(root, workspace, metadata, general, pad=pad)
        drift.extend(layout_count_check(root, costs, padded, pad))
    (root / "placement-counts.md").write_text(f"# Repeated exclusive counts for all neutral layouts\n\nAll 400 cases, both revisions, two processes each, all 0/16/64/256-byte pads. Ir/Dr/Dw drift relative to the same revision's unpadded binary: {len(drift)} differences. Bcm/I1mr are retained separately in placement-counts-*.json; neither changes the strict criterion.\n")
    if drift:
        raise SystemExit("Neutral placement changed executed counts; no placement attribution accepted")
    (root / "counts-gate.json").write_text(json.dumps(dict(strict_ordered_counts_pass=True,
                                                         neutral_layout_counts_identical=True,
                                                         prepared_exception_max=dict(Ir=8, Dr=0, Dw=2))) + "\n")


def timings_phase(general, workspace):
    root = general / "session-proof"
    gate = json.loads((root / "counts-gate.json").read_text())
    if not gate["strict_ordered_counts_pass"] or not gate["neutral_layout_counts_identical"]:
        raise ValueError("count gate not satisfied")
    metadata = json.loads((general / "metadata.json").read_text())
    separate_passes(root, general, metadata)
    placement(root, workspace, metadata)
    quality_and_controls(root, general, metadata)


if __name__ == "__main__":
    if len(sys.argv) != 4 or sys.argv[1] not in ("counts", "timings"):
        raise SystemExit("usage: session_proof.py counts|timings GENERAL_REPORT WORKSPACE")
    phase = counts_phase if sys.argv[1] == "counts" else timings_phase
    phase(Path(sys.argv[2]), Path(sys.argv[3]))
