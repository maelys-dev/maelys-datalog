#!/usr/bin/env python3
# SPDX-License-Identifier: MPL-2.0
"""Fail closed on incomplete diagnostic evidence; report every declared layout."""
import csv
import math
from pathlib import Path
import re
import statistics
import sys
from compare_runs import compare, display

ROLES = ("A", "B", "C")
PADS = (0, 16, 64, 256)


def timing(path):
    with path.open(newline="") as stream:
        rows = list(csv.DictReader(stream))
    if len(rows) != 1000 or [r["sample"] for r in rows] != [str(i) for i in range(1000)]:
        raise ValueError(f"sample inventory: {path}")
    values = sorted(float(r["elapsed_us"]) for r in rows)
    if any(r["result"] != "17" for r in rows) or any(not math.isfinite(v) or v <= 0 for v in values):
        raise ValueError(f"invalid timing/result: {path}")
    return dict(min_us=values[0], median_us=statistics.median(values), p95_us=values[math.ceil(.95 * len(values)) - 1])


def instructions(path):
    text = path.read_text()
    if re.findall(r"^events: (.+)$", text, re.M) != ["Ir"]:
        raise ValueError(f"unexpected Callgrind events: {path}")
    values = re.findall(r"^summary: (\d+)$", text, re.M)
    if len(values) != 1 or int(values[0]) <= 0:
        raise ValueError(f"missing instruction count: {path}")
    return int(values[0])


def functions(path, exclude_insertion=True):
    costs = {}
    for line in path.read_text().splitlines():
        match = re.match(r"^\s*([\d,]+)\s+\([\d. ]+%\)\s+(\S+):(\S+)", line)
        if match:
            name = re.sub(r"'\d+$", "", match[3])
            costs[name] = costs.get(name, 0) + int(match[1].replace(",", ""))
    if not costs:
        raise ValueError(f"missing function costs: {path}")
    for name in ("maelys_datalog_edb_add_fact", "maelys_datalog_edb_add_runtime_symbol_fact"):
        if exclude_insertion and costs.get(name, 0):
            raise ValueError(f"preparation leaked into count: {path}/{name}")
    return costs


def address(path, name):
    matches = re.findall(r"^([0-9a-f]+) [0-9a-f]+ [tT] " + name + r"$", path.read_text(), re.M)
    if len(matches) != 1:
        raise ValueError(f"missing symbol: {path}/{name}")
    return int(matches[0], 16)


def fixture_size(root):
    cases = re.findall(r"^case=(.+)$", (root / "revisions.txt").read_text(), re.M)
    if not cases:  # Older diagnostic artifacts predate the selectable case.
        return "2048"
    if len(cases) != 1 or cases[0] not in ("solver_size_pure/1024", "solver_size_pure/2048"):
        raise ValueError("invalid diagnostic case")
    return cases[0].split("/")[1]


def data_layout(path):
    with path.open(newline="") as stream:
        rows = list(csv.DictReader(stream))
    if len({r["key"] for r in rows}) != len(rows):
        raise ValueError(f"duplicate layout key: {path}")
    values = {r["key"]: int(r["value"]) for r in rows}
    required = ["sizeof.ruleset", "alignof.ruleset", "address_mod64.ruleset"]
    required += ["offsetof.ruleset." + field for field in ("strata", "symbols", "registry", "facts", "rules")]
    if any(key not in values for key in required) or any(v < 0 for v in values.values()):
        raise ValueError(f"missing/invalid data layout: {path}")
    alignment = values["alignof.ruleset"]
    if not alignment or alignment & (alignment - 1) or values["address_mod64.ruleset"] >= 64:
        raise ValueError(f"invalid data alignment: {path}")
    for key, offset in values.items():
        if key.startswith("offsetof.ruleset."):
            address_key = key.replace("offsetof.", "address_mod64.")
            if offset >= values["sizeof.ruleset"] or values.get(address_key) != (values["address_mod64.ruleset"] + offset) % 64:
                raise ValueError(f"inconsistent member layout: {path}/{key}")
    return values


def report(root):
    size = fixture_size(root)
    print(f"# LARGE solver_size_pure / {size}: instructions and layout\n")
    print("```\n" + (root / "revisions.txt").read_text().rstrip() + "\n```\n")
    print("A = baseline; B = original candidate; C = revised candidate. Clang -O2, LARGE, "
          "same untouched historical fixture/payload included in a diagnostic driver. "
          "The additional driver changes the binary relative to the full benchmark: "
          "compare the complete rerun separately. All objects compile before timing.\n")
    print("Unreachable 0/16/64/256-byte text padding precedes the EDB object at link time; "
          "each revision reuses identical compiled objects. All declared variants are reported. "
          "No priority or affinity changes. Each unpadded revision has two A/A pairs before "
          "two interleaved A/B/C rounds over every layout. Each timed pass has 500 warmups "
          "and 1000 checked samples. Per-pass medians/p95s are aggregated by their median; "
          "p95 is not pooled and floors are not confidence intervals.\n")
    print("Callgrind counts software-executed Ir events in the solve payload only, after "
          "8 untimed warmups, in two separate processes. Preparation, clocks and oracle "
          "checks are excluded. This is not a hardware retired-instruction counter. "
          "Equal Ir rules out extra instruction volume for this fixture; it does not "
          "establish equal cycles/cache behavior or alone prove a layout cause.\n")
    layouts = list(root.glob("*.layout.csv"))
    if layouts:
        if len(layouts) != len(ROLES) * len(PADS):
            raise ValueError("incomplete data-layout inventory")
        print("| Revision | Padding | sizeof ruleset | symbols offset | Ruleset address mod 64 | Symbols address mod 64 |\n"
              "| --- | ---: | ---: | ---: | ---: | ---: |")
        for role in ROLES:
            for pad in PADS:
                data = data_layout(root / f"{role}-{pad}.layout.csv")
                print(f"| {role} | {pad} | {data['sizeof.ruleset']} | {data['offsetof.ruleset.symbols']} | "
                      f"{data['address_mod64.ruleset']} | {data['address_mod64.ruleset.symbols']} |")
        print("\nLayout snapshots also retain all prior ruleset member offsets, enclosing context "
              "size/offset, and element sizes/alignments. Modulo 64 is an explicit reference, "
              "not a portable cache-line guarantee. Text padding controls code addresses, "
              "not member offsets. Restoring offsets can change generated code and does not "
              "by itself prove a cache mechanism or zero overhead.\n")
    else:
        print("Data-layout snapshots were not collected by this older driver.\n")
    ir = {}
    print("| Revision | Padding | Ir run 1 | Ir run 2 | EDB add address | Solve address |\n"
          "| --- | ---: | ---: | ---: | --- | --- |")
    for role in ROLES:
        origin = None
        for pad in PADS:
            ir[role, pad] = [instructions(root / f"ir-{role}-{pad}-{p}.out") for p in (1, 2)]
            for p in (1, 2):
                with (root / f"ir-{role}-{pad}-{p}.csv").open(newline="") as stream:
                    if list(csv.DictReader(stream)) != [dict(sample="0", elapsed_us="0.000000", result="17")]:
                        raise ValueError("invalid counted oracle")
            edb = address(root / f"{role}-{pad}.nm", "maelys_datalog_edb_add_fact")
            solve = address(root / f"{role}-{pad}.nm", "maelys_datalog_solve_once")
            if pad == 0:
                origin = (edb, solve)
            elif (edb - origin[0], solve - origin[1]) != (pad, pad):
                raise ValueError(f"unexpected placement perturbation: {role}/{pad}")
            print(f"| {role} | {pad} | {ir[role, pad][0]} | {ir[role, pad][1]} | {edb:#x} | {solve:#x} |")
    print("\nRaw instruction-level profiles, complete exclusive function annotations, symbol "
          "maps, disassembly, binary/harness hashes and all samples accompany this report. "
          "Count differences require function/instruction inspection, not a timing explanation.\n")
    costs = {}
    for role in ROLES:
        for pad in PADS:
            for pass_number in (1, 2):
                costs[role, pad, pass_number] = functions(root / f"ir-{role}-{pad}-{pass_number}.functions.txt")
    print("| Function (exclusive Ir, unpadded first run) | A | B | C | B−A | C−A |\n"
          "| --- | ---: | ---: | ---: | ---: | ---: |")
    baseline = costs["A", 0, 1]
    original = costs["B", 0, 1]
    revised = costs["C", 0, 1]
    changed = False
    for name in sorted(baseline.keys() | original.keys() | revised.keys()):
        a, b, c = (f.get(name, 0) for f in (baseline, original, revised))
        if a != b or a != c:
            changed = True
            print(f"| {name} | {a} | {b} | {c} | {b-a:+d} | {c-a:+d} |")
    if not changed:
        print("| No exclusive function-count difference | | | | | |")
    print()
    aa = {r: [timing(root / f"{r}-aa-{p}.csv") for p in (1, 2, 3, 4)] for r in ROLES}
    runs = {(r, pad): [timing(root / f"{r}-{pad}-{p}.csv") for p in (1, 2)] for r in ROLES for pad in PADS}
    print("| Comparison | Metric | Reference µs | Variant µs | Ratio | A/A floor | Observation |\n"
          "| --- | --- | ---: | ---: | ---: | ---: | --- |")
    comparisons = [(f"A/0 → {r}/{pad}", "A", ("A", 0), (r, pad)) for r in ("B", "C") for pad in PADS]
    comparisons += [(f"B/0 → C/{pad}", "B", ("B", 0), ("C", pad)) for pad in PADS]
    comparisons += [(f"{r}/0 → {r}/{pad}", r, (r, 0), (r, pad)) for r in ROLES for pad in PADS[1:]]
    for label, floor_role, reference, variant in comparisons:
        for metric in ("median_us", "p95_us"):
            a, b, ratio, floor, verdict = compare(aa[floor_role], runs[reference], runs[variant], metric)
            print(f"| {label} | {metric} | {a:.6f} | {b:.6f} | {display(ratio)} | {display(floor, True)} | {verdict} |")
    print("\nA timing change with unchanged executed instructions under a verified placement "
          "perturbation demonstrates placement sensitivity on this run. It does not explain "
          "every other case or authorize a merge. A stable effect across these four layouts "
          "does not rule out other layout effects. No automatic acceptance follows.\n")
    print("Method references: [Callgrind manual](https://valgrind.org/docs/manual/cl-manual.html), "
          "[Mytkowicz et al., ASPLOS 2009](https://sape.inf.usi.ch/publications/asplos09.html).")


if __name__ == "__main__":
    try:
        if len(sys.argv) != 2:
            raise ValueError("usage: report_solver_layout.py OUTPUT")
        report(Path(sys.argv[1]))
    except (ValueError, OSError, KeyError) as error:
        raise SystemExit(f"Invalid or incomplete diagnostic evidence: {error}") from error
