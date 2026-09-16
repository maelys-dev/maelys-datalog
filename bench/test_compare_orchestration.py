# SPDX-License-Identifier: MPL-2.0
"""Synthetic executables test ordering/build counts, never performance."""
import json
import os
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest


ROOT = Path(__file__).resolve().parents[1]
PAYLOAD = r'''#!/usr/bin/env python3
import csv, json, os, sys
from pathlib import Path
target = Path(sys.argv[1])
meta = json.loads((target.parent / "metadata.json").read_text())
binary = Path(sys.argv[0])
kind = binary.name
profile = binary.parent.name.rsplit("-", 1)[1]
role = binary.parent.name.split("-")[1]
with open(os.environ["BENCH_TEST_TRACE"], "a") as trace:
    trace.write(f"{kind} {profile} {target.stem}\n")
if kind == "solver":
    row = dict(benchmark="case", group="g", mode="m", size=1, selectivity=1,
               samples=1000, min_us=1, median_us=2, p95_us=3,
               commit=meta["base" if role == "A" else "head"],
               compiler="synthetic", cflags="-O2", opt_level="-O2")
else:
    row = dict(scenario="crossover", capacity=13, entries=13, distinct_strings=64,
               mode="batch", text_capacity=384, reserved_bytes=1024, samples=301,
               min_us=1, median_us=2, p95_us=3,
               clear_min_us=.01, clear_median_us=.02, clear_p95_us=.03)
with target.open("w", newline="") as stream:
    writer = csv.DictWriter(stream, list(row))
    writer.writeheader()
    writer.writerow(row)
Path(sys.argv[2]).write_text("{}" if kind == "solver" else "synthetic samples\n")
'''


class OrchestrationTest(unittest.TestCase):
    def test_compile_once_and_all_aa_before_ab(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            repo = root / "repo"
            (repo / "bench").mkdir(parents=True)
            for name in ("compare_revisions.sh", "compare_runs.py", "Makefile.compare", "bench_input_edb.c"):
                shutil.copy2(ROOT / "bench" / name, repo / "bench" / name)
            (repo / "bench/bench_datalog.c").write_text("synthetic harness\n")
            source = repo / "src/runtime/maelys_datalog_input_edb.c"
            source.parent.mkdir(parents=True)
            source.write_text("linear fixture\n")
            def git(*args):
                return subprocess.check_output(["git", "-C", str(repo), *args], text=True).strip()
            git("init", "-q", "-b", "main")
            git("config", "user.name", "Fixture")
            git("config", "user.email", "fixture@example.invalid")
            git("add", ".")
            git("-c", "commit.gpgsign=false", "commit", "-qm", "baseline fixture")
            base = git("rev-parse", "HEAD")
            source.write_text("indexed fixture\n")
            git("add", ".")
            git("-c", "commit.gpgsign=false", "commit", "-qm", "candidate fixture")
            head = git("rev-parse", "HEAD")
            git("update-ref", "refs/remotes/origin/candidate", head)
            bins = root / "bin"
            bins.mkdir()
            trace = root / "trace"
            payload = root / "payload"
            payload.write_text(PAYLOAD)
            payload.chmod(0o755)
            scripts = {
                "uname": "#!/bin/sh\necho Linux\n",
                "clang": "#!/bin/sh\necho synthetic-clang\n",
                "make": '''#!/bin/sh
set -eu
printf 'make %s\n' "$*" >> "$BENCH_TEST_TRACE"
for argument in "$@"; do case "$argument" in OUT=*) output=${argument#OUT=} ;; esac; done
mkdir -p "$output"
cp "$BENCH_TEST_PAYLOAD" "$output/solver"
cp "$BENCH_TEST_PAYLOAD" "$output/input"
''',
            }
            for name, body in scripts.items():
                path = bins / name
                path.write_text(body)
                path.chmod(0o755)
            environment = dict(os.environ, PATH=str(bins) + os.pathsep + os.environ["PATH"],
                               TMPDIR=str(root), BENCH_TEST_TRACE=str(trace),
                               BENCH_TEST_PAYLOAD=str(payload))
            output = root / "output"
            result = subprocess.run(["bash", str(repo / "bench/compare_revisions.sh"),
                                     base, "candidate", str(output)],
                                    env=environment, text=True, capture_output=True)
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
            lines = trace.read_text().splitlines()
            self.assertEqual(sum(line.startswith("make ") for line in lines), 4)
            self.assertTrue(all(line.startswith("make -j1 ") for line in lines[:4]))
            self.assertEqual(len(lines), 36)
            measurements = lines[4:]
            self.assertTrue(all("-aa-" in line for line in measurements[:16]))
            self.assertTrue(all("-ab-" in line for line in measurements[16:]))
            for profile in ("SMALL", "LARGE"):
                actual = [line.split()[-1].split("-ab-")[-1] for line in measurements
                          if line.startswith(f"solver {profile} ") and "-ab-" in line]
                self.assertEqual(actual, ["A1", "B1", "A2", "B2"])
            self.assertEqual(json.loads((output / "metadata.json").read_text())["head"], head)
            self.assertIn("indéterminé", (output / "comparison.md").read_text())
            self.assertFalse((repo / "bench/results").exists())
            # Existing artifacts must never be overwritten.
            again = subprocess.run(["bash", str(repo / "bench/compare_revisions.sh"),
                                    base, head, str(output)], env=environment, capture_output=True)
            self.assertNotEqual(again.returncode, 0)
            self.assertEqual(len(trace.read_text().splitlines()), 36)
            # Input is a Git name, never shell code or a Git option.
            marker = root / "injection"
            for ref in ("--help", f"$(touch {marker})"):
                bad = subprocess.run(["bash", str(repo / "bench/compare_revisions.sh"),
                                      ref, head, str(root / "bad")],
                                     env=environment, capture_output=True)
                self.assertNotEqual(bad.returncode, 0)
            self.assertFalse(marker.exists())


if __name__ == "__main__":
    unittest.main()
