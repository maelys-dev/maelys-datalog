#!/usr/bin/env python3
# SPDX-License-Identifier: MPL-2.0
"""Compare compact storage through separately installed SDKs; software counts only."""
import argparse
import hashlib
import json
import re
from pathlib import Path
import subprocess
from report_host_delta import counts

ROOT = Path(__file__).resolve().parents[1]
PROFILES = ("SMALL", "LARGE")
CASES = ("ordinary", "window")
ORDER = ("base1", "base2", "head1", "head2", "base3", "head3", "base4", "head4")

def command(argv, log):
    with log.open("w") as stream:
        subprocess.run(argv, check=True, stdout=stream, stderr=subprocess.STDOUT)

def physical_functions(region):
    functions = {}
    for name, vector in region["functions"].items():
        # Callgrind appends apostrophe + digits for synthetic recursion contexts.
        # C identifiers cannot contain these suffixes; they share one ELF symbol.
        symbol = re.sub(r"'\d+$", "", name)
        current = functions.setdefault(symbol, [0, 0, 0])
        for i in range(3):
            current[i] += vector[i]
    return {**region, "functions": functions}

def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("base")
    parser.add_argument("head")
    parser.add_argument("output", type=Path)
    args = parser.parse_args()
    out = args.output.resolve()
    out.mkdir(parents=True, exist_ok=False)
    refs = {role: subprocess.check_output(["git", "rev-parse", ref], cwd=ROOT, text=True).strip()
            for role, ref in (("base", args.base), ("head", args.head))}
    if subprocess.check_output(["git", "rev-parse", "HEAD"], cwd=ROOT, text=True).strip() != refs["head"]:
        parser.error("tooling checkout must be the exact head")
    if subprocess.check_output(["git", "status", "--porcelain"], cwd=ROOT, text=True):
        parser.error("tooling checkout must be clean")
    manifest = {"schema": 1, "revisions": refs, "profiles": PROFILES, "cases": CASES, "order": ORDER,
        "regions_per_process": 2, "sdk_mode": "CMake Release, clang -O3 -DNDEBUG",
        "driver_mode": "clang -O3 -g -UNDEBUG; identical public consumer source",
        "scope": "Complete ordinary request (create/solve/query/release/destroy), 93 symbol facts, 16 requests per region; reference last-N 64-symbol window, 130 pushes including expiration/publication/old-result release per region. Fixture setup and final window teardown excluded; driver/status checks remain visible.",
        "limits": "Ir/Dr/Dw are software counts, not latency or hardware counters. Local Docker Linux ARM64 does not establish hosted-runner performance.",
        "sources": {f: hashlib.sha256((ROOT/f).read_bytes()).hexdigest() for f in
            ("bench/compact_storage.c", "bench/compare_compact_storage.py", "bench/report_host_delta.py")}}
    (out/"manifest.json").write_text(json.dumps(manifest, indent=2)+"\n")
    (out/"cpu.txt").write_text(Path("/proc/cpuinfo").read_text())
    for name, argv in (("compiler", ["clang", "--version"]), ("valgrind", ["valgrind", "--version"]), ("platform", ["uname", "-a"])):
        command(argv, out/(name+".txt"))
    for role, sha in refs.items():
        source = out/(role+"-source"); source.mkdir()
        command(["git", "archive", "--format=tar", "--output="+str(out/(role+".tar")), sha], out/(role+"-archive.log"))
        command(["tar", "-xf", str(out/(role+".tar")), "-C", str(source)], out/(role+"-extract.log"))
        for profile in PROFILES:
            path = out/profile/role; path.mkdir(parents=True)
            command(["cmake", "-S", str(source), "-B", str(path/"build"), "-DCMAKE_BUILD_TYPE=Release", "-DBUILD_TESTING=OFF",
                "-DCMAKE_C_COMPILER=clang", "-DMAELYS_DATALOG_PROFILE_LARGE="+("ON" if profile=="LARGE" else "OFF"),
                "-DCMAKE_INSTALL_PREFIX="+str(path/"sdk")], path/"configure.log")
            command(["cmake", "--build", str(path/"build"), "--target", "maelys_datalog", "-j4"], path/"build.log")
            for part in ("sdk", "sdk-static"):
                command(["cmake", "--install", str(path/"build"), "--component", part], path/(part+".log"))
            binary = path/"compact-storage"
            command(["clang", "-std=c11", "-O3", "-g", "-UNDEBUG", "-Wall", "-Wextra", "-Werror", "-DMAELYS_BENCH_COUNT",
                "-I"+str(path/"sdk/include"), str(ROOT/"bench/compact_storage.c"), str(path/"sdk/lib/libmaelys_datalog.a"), "-o", str(binary)], path/"driver-build.log")
            command(["nm", "-n", str(binary)], path/"symbols.txt")
            command(["objdump", "-dr", str(binary)], path/"disassembly.txt")
            (path/"binary.sha256").write_text(hashlib.sha256(binary.read_bytes()).hexdigest()+"\n")
    # Every SDK and driver is finished before the first measured process starts.
    comparisons = []
    for profile in PROFILES:
        measured = {}
        for label in ORDER:
            role = label[:-1]
            for case in CASES:
                path = out/profile/label/case; path.mkdir(parents=True)
                command(["valgrind", "--tool=callgrind", "--cache-sim=yes", "--branch-sim=no", "--instr-atstart=no",
                    "--error-exitcode=3", "--callgrind-out-file="+str(path/"counts"), str(out/profile/role/"compact-storage"), case], path/"checked.log")
                regions = [r for f in sorted(path.glob("counts.*")) if (raw := counts(f)) is not None for r in [physical_functions(raw)]]
                if len(regions) != 2 or regions[0] != regions[1]:
                    raise ValueError(f"nonidentical internal repetitions: {profile}/{label}/{case}")
                measured[label, case] = regions[0]
        for case in CASES:
            for role in ("base", "head"):
                first = measured[role+"1", case]
                if any(measured[role+str(n), case] != first for n in (2,3,4)):
                    raise ValueError(f"nonidentical process repetitions: {profile}/{role}/{case}")
            a,b = measured["base1",case], measured["head1",case]
            comparisons.append({"profile": profile, "case": case, "base": a, "head": b,
                "change_percent": [100*(y/x-1) if x else None for x,y in zip(a["total"],b["total"])]})
    result = {"schema": 1, "manifest_sha256": hashlib.sha256((out/"manifest.json").read_bytes()).hexdigest(),
        "regions": len(PROFILES)*len(ORDER)*len(CASES)*2, "identical_repetitions": True, "comparisons": comparisons}
    (out/"report.json").write_text(json.dumps(result, indent=2)+"\n")
    for row in comparisons:
        print(row["profile"], row["case"], row["base"]["total"], row["head"]["total"], row["change_percent"])

if __name__ == "__main__":
    main()
