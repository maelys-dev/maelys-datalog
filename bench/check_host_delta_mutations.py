#!/usr/bin/env python3
# SPDX-License-Identifier: MPL-2.0
"""Prove that the oracle/atomicity checks detect transaction and linear-composition defects."""
import argparse
import json
import subprocess
import tempfile
from pathlib import Path

MUTATIONS = {
    "wrong_owner": ("b != expected_owner ||", "0 ||"),
    "skip_bank_catchup": ("for (size_t step = 0; step < op->count; ++step)",
                          "for (size_t step = op->count-1; step < op->count; ++step)"),
    "publish_on_rejection": ("current = NULL;\n    if (rc) return rc;",
                             "current = NULL;\n    if (rc) { delta_accept(b,logical,op->delta); return rc; }"),
    "skip_raw_rejection": ("if (rc) return rc;\n    }\n    size_t n = b->committed.count;",
                           "/* discarded validation error */\n    }\n    size_t n = b->committed.count;"),
    "skip_linear_removals": ("if (nr) {", "if (0 && nr) {"),
    "keep_existing_additions": ("if (base < n && !maelys_datalog_fact_cmp(&facts[base], &added[i])) continue;",
                                "if (0 && base < n && !maelys_datalog_fact_cmp(&facts[base], &added[i])) continue;"),
    "reject_exact_capacity": ("if (fresh > capacity - n)", "if (fresh >= capacity - n)"),
    "merge_wrong_order": ("maelys_datalog_fact_cmp(&facts[left-1], &added[right-1]) > 0",
                          "maelys_datalog_fact_cmp(&facts[left-1], &added[right-1]) < 0"),
}


def run(build, profile):
    source = Path("bench/host_delta.c").read_text()
    objects = sorted(p for p in (build/"obj").rglob("*.o") if p.name != "maelys_datalog_runtime.o")
    objects += [build/"backend.o", build/"alloc.o"]
    if not objects or not all(p.is_file() for p in objects):
        raise ValueError("build the uninstrumented driver first")
    flags = ["-O2", "-g", "-I.", "-Iinclude", "-Ibench", "-include", "bench/host_delta_alloc.h"]
    if profile == "LARGE":
        flags += ["-DMAELYS_DATALOG_PROFILE_LARGE"]
    results = []
    with tempfile.TemporaryDirectory(prefix="host-delta-mutants-") as directory:
        tmp = Path(directory)
        for name, (old, new) in {"baseline": ("/* Experimental", "/* Experimental"), **MUTATIONS}.items():
            if old not in source:
                raise ValueError(f"mutation location missing: {name}")
            path = tmp/f"{name}.c"
            path.write_text(source.replace(old, new))
            exe = tmp/name
            subprocess.run(["clang", *flags, str(path), *(str(p) for p in objects), "-o", str(exe)], check=True)
            process = subprocess.run([str(exe), "check"], capture_output=True, text=True)
            accepted = process.returncode == 0
            # CHECK prints the temporary .c path; reject compiler/loader failures.
            if name != "baseline":
                accepted = process.returncode != 0 and str(path)+":" in process.stderr
            results.append(dict(mutation=name, detected=accepted, exit=process.returncode, diagnostic=process.stderr))
            if not accepted:
                raise RuntimeError(json.dumps(results, indent=2))
    return results


if __name__ == "__main__":
    parser = argparse.ArgumentParser()
    parser.add_argument("build", type=Path)
    parser.add_argument("--profile", choices=["SMALL", "LARGE"], default="SMALL")
    args = parser.parse_args()
    print(json.dumps(run(args.build.resolve(), args.profile), indent=2))
