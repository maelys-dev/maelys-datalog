#!/usr/bin/env python3
# SPDX-License-Identifier: MPL-2.0
"""Fail-closed, exclusive software-count report for the bounded A/B/L/T experiment."""
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


def receipts(path, role, scope):
    with path.open() as stream:
        raw = list(csv.DictReader(stream))
    rows = {}
    for row in raw:
        if not row["case"].startswith(f"{role}/{scope}/") or row["transactions"] != "8":
            raise ValueError(f"wrong variant/scope/length: {path}")
        key = row["case"].split("/", 1)[1]
        if key in rows:
            raise ValueError(f"duplicate receipt: {path}/{key}")
        rows[key] = row
    return rows


def load_run(directory):
    role = directory.name[0]
    scope = directory.name.split("-", 1)[1]
    rows = {}
    for path in sorted(directory.glob("counts.*")):
        row = counts(path)
        if row:
            if not row["label"].startswith(f"{role}/{scope}/"):
                raise ValueError(f"wrong count variant/scope: {path}")
            key = row["label"].split("/", 1)[1]
            if key in rows:
                raise ValueError(f"duplicate region {key}")
            rows[key] = row
    observed = receipts(directory/"receipts.csv", role, scope)
    if not rows or not observed:
        raise ValueError(f"missing evidence: {directory}")
    expected = set()
    for key in observed:
        phases = ("failure",) if "/abort" in key else ("init", "first", "steady")
        expected.update(f"{key}/{phase}" for phase in phases)
    if expected != rows.keys():
        raise ValueError(f"region inventory mismatch: {directory}")
    return rows, observed


def compare(base, candidate):
    host_saved = [base["host"][i]-candidate["host"][i] for i in range(3)]
    return dict(host_saved=host_saved,
                total_saved=[base["total"][i]-candidate["total"][i] for i in range(3)],
                host_saved_percent=100*host_saved[0]/base["host"][0])


def compare_variants(row):
    return {f"{candidate}/{base}": compare(row[base], row[candidate])
            for base, candidate in (("A", "B"), ("A", "L"), ("B", "L"),
                                    ("A", "T"), ("B", "T"), ("L", "T"))}


def tombstone_exceptions(rows):
    exceptions = []
    for row in rows:
        if not (row['region'].startswith('engine/') and row['region'].endswith('/steady')):
            continue
        base = min(('B', 'L'), key=lambda role: row[role]['host'][0])
        extra = row['T']['host'][0] - row[base]['host'][0]
        if extra <= 0:
            continue
        functions = {fn: [row['T']['functions'].get(fn, [0, 0, 0])[i] -
                          row[base]['functions'].get(fn, [0, 0, 0])[i] for i in range(3)]
                     for fn in row['T']['functions'].keys() | row[base]['functions'].keys()}
        exceptions.append(dict(profile=row['profile'], region=row['region'], baseline=base,
                               host_extra_Ir=extra, functions={k:v for k,v in sorted(functions.items()) if any(v)}))
    return exceptions


def report(root):
    metadata = json.loads((root/"experiment.json").read_text())
    expected_metadata = dict(schema=3, variants=["A", "B", "L", "T"],
                             order=["A1", "B1", "L1", "T1", "T2", "L2", "B2", "A2"], transactions=8)
    if metadata != expected_metadata:
        raise ValueError("expected the declared schema-3 A/B/L/T experiment; replay older schemas with their original reporter")
    roles = metadata["variants"]
    comparisons = []
    repeats = []
    for profile, ordinary in (("SMALL", 40), ("LARGE", 60)):
        undefined = (root/profile/"backend-undefined.txt").read_text().splitlines()
        allowed = {"maelys_datalog_backend_charge", "maelys_datalog_backend_emit", "maelys_datalog_program_fact", "maelys_datalog_program_info", "maelys_datalog_program_rule"}
        if {line.split()[-1] for line in undefined} != allowed:
            raise ValueError(f"backend external callees need an attribution audit: {profile}")
        for scope in ("engine", "caller"):
            runs = {tag: load_run(root/profile/f"{tag}-{scope}") for tag in metadata["order"]}
            a, ar = runs["A1"]
            if len(ar) != ordinary + (6 if scope == "engine" else 0):
                raise ValueError(f"fixture inventory mismatch: {profile}/{scope}")
            for role in roles:
                first, observed = runs[role+"1"]
                second, again = runs[role+"2"]
                if ar.keys() != observed.keys() or first.keys() != second.keys() or observed != again:
                    raise ValueError(f"repetition inventory/output mismatch {profile}/{scope}/{role}")
                for key in ar:
                    for field in ("transactions", "digest", "final_unique_facts", "retained_bytes", "bank_bytes", "setup_calls", "setup_bytes"):
                        if ar[key][field] != observed[key][field]:
                            raise ValueError(f"receipt mismatch: {profile}/{role}/{key}/{field}")
                for key in sorted(first):
                    equal = all(first[key][f] == second[key][f] for f in ("total", "functions", "residual"))
                    repeats.append(dict(profile=profile, role=role, region=key, identical=equal))
                    if not equal and not key.endswith("/init"):
                        raise ValueError(f"operation count drift: {profile}/{role}/{key}")
            memory = {}
            for role in roles:
                memory[role] = {}
                path = root/profile/"memory"/f"{role}-{scope}-bytes.csv"
                with path.open() as stream:
                    for entry in csv.reader(stream):
                        if len(entry)!=4 or not entry[0].startswith(f"{role}/{scope}/"):
                            raise ValueError(f"invalid byte observation: {path}")
                        key = entry[0].split("/",1)[1]
                        if key in memory[role]:
                            raise ValueError(f"duplicate byte observation: {path}/{key}")
                        memory[role][key] = list(map(int,entry[1:]))
                observed = receipts(root/profile/"memory"/f"{role}-{scope}.csv",role,scope)
                if observed != runs[role+"1"][1] or memory[role].keys() != a.keys():
                    raise ValueError(f"byte observer changed outputs/inventory: {profile}/{role}/{scope}")
            for key in sorted(a):
                row = dict(profile=profile, region=key,
                           receipts={role:runs[role+"1"][1][key.rsplit("/",1)[0]] for role in roles})
                for role in roles:
                    measured = runs[role+"1"][0][key]
                    groups = {}
                    for fn, values in measured["functions"].items():
                        group = groups.setdefault(category(fn), [0, 0, 0])
                        for i, value in enumerate(values):
                            group[i] += value
                    backend = vector(measured["functions"], lambda fn: category(fn)=="backend_exclusive")
                    host = vector(measured["functions"], lambda fn: category(fn)!="backend_exclusive")
                    row[role] = dict(total=measured["total"], host=host, backend=backend,
                        residual=measured["residual"], groups=groups, functions=measured["functions"],
                        explicit_primitive_bytes=dict(zip(("copy", "move", "set"), memory[role][key])))
                    if role != "A" and backend != row["A"]["backend"]:
                        raise ValueError(f"provider work changed: {profile}/{role}/{key}")
                row["pairs"] = compare_variants(row)
                comparisons.append(row)
    return dict(schema=3, experiment=metadata, events=EVENTS, repetitions=repeats,
                comparisons=comparisons, tombstone_exceptions=tombstone_exceptions(comparisons))


def write(root):
    result = report(root)
    (root/"report.json").write_text(json.dumps(result, indent=2)+"\n")
    lines = ["# Host delta → snapshot: software-count experiment", "", "Local Linux/ARM64 Docker; same Clang -O2 binary for A/B/L/T. A=snapshot, B=repeated moves, L=linear composition, T=tombstones and bounded compaction. No timing or hardware counters.",
        "Host = exclusive non-provider function costs, including shared runtime/libc/driver costs. Client-request boundary residual is separate; totals retain it.",
        "Groups are exclusive function buckets, not fully separated semantic phases: shared/inlined helpers remain in the shared bucket. Init includes the common over-reserved diagnostic scaffolding.", "",
        "SMALL: 8/64; 256 exceeds its per-predicate bound. LARGE: 8/64/256. Window N counts occurrences; initial live set is N/2, not N.",
        "First = one transaction; steady = seven; failure = eight attempts. Caller scope includes the modeled occurrence ledger and full snapshot or delta production; it is not the production window adapter.", "",
        f"Exact repeated operation regions: {sum(x['identical'] for x in result['repetitions'] if not x['region'].endswith('/init'))}. Init drifts (retained): {sum(not x['identical'] for x in result['repetitions'] if x['region'].endswith('/init'))}.", "",
        "| Profile | Region | Host A Ir | Host B Ir | Host L Ir | Host T Ir | L/A host saved | L/B host saved | T/A host saved | T/B host saved | T/L host saved |", "|---|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|"]
    for row in result["comparisons"]:
        values = ' | '.join(f"{row['pairs'][pair]['host_saved_percent']:.2f}%" for pair in ('L/A','L/B','T/A','T/B','T/L'))
        lines.append(f"| {row['profile']} | {row['region']} | {row['A']['host'][0]} | {row['B']['host'][0]} | {row['L']['host'][0]} | {row['T']['host'][0]} | {values} |")
    lines += ['', '## Every T > min(B,L) exception in engine steady regions', '',
              'Function deltas below are exclusive Ir against the named cheaper path; all Ir/Dr/Dw deltas are in JSON.', '']
    for row in result['tombstone_exceptions']:
        lines += [f"### {row['profile']} {row['region']}", '',
                  f"T minus {row['baseline']}: +{row['host_extra_Ir']} host Ir across seven transactions.", '',
                  '| Exclusive function | T minus baseline Ir |', '|---|---:|']
        lines.extend(f'| {fn} | {values[0]:+d} |' for fn,values in row['functions'].items() if values[0])
        lines.append('')
    lines += ["", "Positive savings justify reviewing this restricted host path, not an ABI decision. Full-replace losses, reservation costs, unsupported language/vocabulary modes and the absence of real window/explanation integration remain material limitations.", ""]
    (root/"report.md").write_text("\n".join(lines))
    with (root/"SHA256SUMS").open("w") as stream:
        for path in sorted(root.rglob("*")):
            if path.is_file() and path.name != "SHA256SUMS":
                stream.write(f"{hashlib.sha256(path.read_bytes()).hexdigest()}  {path.relative_to(root)}\n")


if __name__ == "__main__":
    write(Path(sys.argv[1]).resolve())
