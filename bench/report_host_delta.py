#!/usr/bin/env python3
# SPDX-License-Identifier: MPL-2.0
"""Fail-closed, exclusive software-count report for the bounded A/B experiment."""
import csv
import hashlib
import json
import re
import sys
from pathlib import Path

EVENTS = ("Ir", "Dr", "Dw")


def counts(path):
    """Read exclusive costs; the cost record following calls= is inclusive."""
    names = {}
    functions = {}
    current = None
    skip = False
    label = None
    summary = totals = None
    event_names = None
    for line in path.read_text().splitlines():
        if line.startswith("desc: Trigger: Client Request: "):
            label = line.split("Client Request: ", 1)[1]
        elif line.startswith("events: "):
            event_names = line.split()[1:]
            if event_names[:3] != list(EVENTS):
                raise ValueError(f"unexpected events: {path}")
        elif line.startswith(("summary: ", "totals: ")):
            values = tuple(map(int, line.split()[1:4]))
            if len(values) != 3:
                raise ValueError(f"missing counters: {path}")
            if line.startswith("summary:"):
                summary = values
            else:
                totals = values
        elif line.startswith(("fn=", "cfn=")):
            text = line.split("=", 1)[1]
            match = re.fullmatch(r"\((\d+)\)(?: (.*))?", text)
            if match:
                key, name = match.groups()
                if name is not None:
                    names[key] = name
                text = names[key]
            if line.startswith("fn="):
                current = text
                functions.setdefault(current, [0, 0, 0])
        elif line.startswith("calls="):
            skip = True
        elif re.match(r"^(?:\*|[+-]?\d+) ", line):
            if skip:
                skip = False
                continue
            if current is None or event_names is None:
                raise ValueError(f"unowned cost: {path}")
            values = list(map(int, line.split()[1:]))
            values += [0] * (3-len(values))
            for i in range(3):
                functions[current][i] += values[i]
    if label is None:
        return None  # final empty process dump, not a requested region
    functions = {k: v for k, v in functions.items() if any(v)}
    accounted = tuple(sum(v[i] for v in functions.values()) for i in range(3))
    if accounted != totals or summary is None or any(a > b for a, b in zip(accounted, summary)):
        raise ValueError(f"unreconciled exclusive costs: {path}: {accounted}/{totals}/{summary}")
    return dict(label=label, total=summary, functions=functions,
                residual=[summary[i]-accounted[i] for i in range(3)])


def vector(functions, predicate):
    return [sum(v[i] for k, v in functions.items() if predicate(k)) for i in range(3)]


def category(name):
    if name.startswith("delta_backend_"):
        return "backend_exclusive"
    if name.startswith("delta_") or name in ("recompute_edb_counts", "materialize_input_fact", "reset_transaction_state", "collect_input_symbols", "intern_input_symbols", "reject_transaction") or "materialize_inputs" in name:
        return "host_input_or_transaction"
    if name.startswith("caller_") or name.startswith("model_snapshot") or name.startswith("fact.") or name == "fact":
        return "caller_input_production"
    if "export_fact" in name:
        return "host_export"
    if name.startswith(("maelys_datalog_backend_emit", "maelys_datalog_backend_charge", "maelys_datalog_backend_filter")):
        return "host_output_services"
    if name.startswith(("maelys_datalog_result_free", "maelys_datalog_result_commit", "clear_explanation_cache")):
        return "host_commit_release"
    return "shared_host_or_driver"  # includes libc and inlined/unsplit runtime work


def load_run(directory):
    rows = {}
    for path in directory.glob("counts.*"):
        row = counts(path)
        if row:
            key = row["label"].split("/", 1)[1]
            if key in rows:
                raise ValueError(f"duplicate region {key}")
            rows[key] = row
    with (directory / "receipts.csv").open() as stream:
        receipts = {r["case"].split("/", 1)[1]: r for r in csv.DictReader(stream)}
    if not rows or not receipts:
        raise ValueError(f"missing evidence: {directory}")
    expected = set()
    for key in receipts:
        phases = ("failure",) if "/abort" in key else ("init", "first", "steady")
        expected.update(f"{key}/{phase}" for phase in phases)
    if expected != rows.keys():
        raise ValueError(f"region inventory mismatch: {directory}")
    return rows, receipts


def report(root):
    comparisons = []
    repeats = []
    for profile, ordinary in (("SMALL", 40), ("LARGE", 60)):
        undefined = (root/profile/"backend-undefined.txt").read_text().splitlines()
        allowed = {"maelys_datalog_backend_charge", "maelys_datalog_backend_emit", "maelys_datalog_program_fact", "maelys_datalog_program_info", "maelys_datalog_program_rule"}
        if {line.split()[-1] for line in undefined} != allowed:
            raise ValueError(f"backend external callees need an attribution audit: {profile}")
        for scope in ("engine", "caller"):
            runs = {tag: load_run(root/profile/f"{tag}-{scope}") for tag in ("A1", "B1", "B2", "A2")}
            a, ar = runs["A1"]
            b, br = runs["B1"]
            if len(ar) != ordinary + (6 if scope == "engine" else 0) or ar.keys() != br.keys():
                raise ValueError(f"fixture inventory mismatch: {profile}/{scope}")
            for key in ar:
                for field in ("transactions", "digest", "final_unique_facts", "retained_bytes", "bank_bytes", "setup_calls", "setup_bytes"):
                    if ar[key][field] != br[key][field]:
                        raise ValueError(f"receipt mismatch: {profile}/{key}/{field}")
            for role in ("A", "B"):
                first, receipts = runs[role+"1"]
                second, again = runs[role+"2"]
                if first.keys() != second.keys() or receipts != again:
                    raise ValueError(f"repetition inventory/output mismatch {profile}/{scope}/{role}")
                for key in first:
                    equal = all(first[key][f] == second[key][f] for f in ("total", "functions", "residual"))
                    repeats.append(dict(profile=profile, role=role, region=key, identical=equal))
                    if not equal and not key.endswith("/init"):
                        raise ValueError(f"operation count drift: {profile}/{role}/{key}")
            memory = {}
            for role in ("A", "B"):
                with (root/profile/"memory"/f"{role}-{scope}-bytes.csv").open() as stream:
                    memory[role] = {r[0].split("/",1)[1]:list(map(int,r[1:])) for r in csv.reader(stream)}
                with (root/profile/"memory"/f"{role}-{scope}.csv").open() as stream:
                    observed = {r["case"].split("/",1)[1]:r for r in csv.DictReader(stream)}
                if observed != runs[role+"1"][1] or memory[role].keys() != a.keys():
                    raise ValueError(f"byte observer changed outputs/inventory: {profile}/{role}/{scope}")
            for key in a:
                pair = dict(profile=profile, region=key, receipts={"A": ar[key.rsplit("/",1)[0]], "B": br[key.rsplit("/",1)[0]]})
                for role, run in (("A", a), ("B", b)):
                    row = run[key]
                    groups = {}
                    for fn, values in row["functions"].items():
                        group = groups.setdefault(category(fn), [0, 0, 0])
                        for i, value in enumerate(values):
                            group[i] += value
                    backend = vector(row["functions"], lambda fn: category(fn)=="backend_exclusive")
                    host = vector(row["functions"], lambda fn: category(fn)!="backend_exclusive")
                    pair[role] = dict(total=row["total"], host=host, backend=backend,
                        residual=row["residual"], groups=groups, functions=row["functions"],
                        explicit_primitive_bytes=dict(zip(("copy", "move", "set"), memory[role][key])))
                pair["host_saved"] = [pair["A"]["host"][i]-pair["B"]["host"][i] for i in range(3)]
                pair["total_saved"] = [pair["A"]["total"][i]-pair["B"]["total"][i] for i in range(3)]
                pair["host_saved_percent"] = 100*pair["host_saved"][0]/pair["A"]["host"][0]
                comparisons.append(pair)
    return dict(schema=1, events=EVENTS, repetitions=repeats, comparisons=comparisons)


def write(root):
    result = report(root)
    (root/"report.json").write_text(json.dumps(result, indent=2)+"\n")
    lines = ["# Host delta → snapshot: software-count experiment", "", "Local Linux/ARM64 Docker; same Clang -O2 binary for A/B. No timing or hardware counters.",
        "Host = exclusive non-provider function costs, including shared runtime/libc/driver costs. Client-request boundary residual is separate; totals retain it.",
        "Groups are exclusive function buckets, not fully separated semantic phases: shared/inlined helpers remain in the shared bucket. Init includes the common over-reserved diagnostic scaffolding.", "",
        "SMALL: 8/64; 256 exceeds its per-predicate bound. LARGE: 8/64/256. Window N counts occurrences; initial live set is N/2, not N.",
        "First = one transaction; steady = seven; failure = eight attempts. Caller scope includes the modeled occurrence ledger and full snapshot or delta production; it is not the production window adapter.", "",
        f"Exact repeated operation regions: {sum(x['identical'] for x in result['repetitions'] if not x['region'].endswith('/init'))}. Init drifts (retained): {sum(not x['identical'] for x in result['repetitions'] if x['region'].endswith('/init'))}.", "",
        "| Profile | Region | Host A Ir | Host B Ir | Host saved | Total saved Ir |", "|---|---|---:|---:|---:|---:|"]
    for row in result["comparisons"]:
        lines.append(f"| {row['profile']} | {row['region']} | {row['A']['host'][0]} | {row['B']['host'][0]} | {row['host_saved_percent']:.2f}% | {row['total_saved'][0]} |")
    lines += ["", "Positive savings justify reviewing this restricted host path, not an ABI decision. Full-replace losses, reservation costs, unsupported language/vocabulary modes and the absence of real window/explanation integration remain material limitations.", ""]
    (root/"report.md").write_text("\n".join(lines))
    with (root/"SHA256SUMS").open("w") as stream:
        for path in sorted(root.rglob("*")):
            if path.is_file() and path.name != "SHA256SUMS":
                stream.write(f"{hashlib.sha256(path.read_bytes()).hexdigest()}  {path.relative_to(root)}\n")


if __name__ == "__main__":
    write(Path(sys.argv[1]).resolve())
