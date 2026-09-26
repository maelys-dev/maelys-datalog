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
binary = Path(sys.argv[0])
kind = binary.name
mode = sys.argv.pop(1) if kind == "explanations" else None
target = Path(sys.argv[1])
meta = json.loads((target.parent / "metadata.json").read_text())
profile = binary.parent.name.rsplit("-", 1)[1]
role = binary.parent.name.split("-")[1]
with open(os.environ["BENCH_TEST_TRACE"], "a") as trace:
    trace.write(f"{kind} {profile} {target.stem}\n")
if kind == "solver":
    row = dict(benchmark="case", group="g", mode="m", size=1, selectivity=1,
               samples=1000, min_us=1, median_us=2, p95_us=3,
               commit=(binary.parent / "revision.txt").read_text().strip(),
               compiler="synthetic", cflags="-O2", opt_level="-O2")
elif kind == "input":
    row = dict(scenario="crossover", capacity=13, entries=13, distinct_strings=64,
               mode="batch", text_capacity=384, reserved_bytes=1024, samples=301,
               min_us=1, median_us=2, p95_us=3,
               clear_min_us=.01, clear_median_us=.02, clear_p95_us=.03)
elif kind == "sessions":
    row = dict(policy="inert", order="sorted", values="integer", size="8", entries=8, edb_limit=2048,
               samples=301, min_us=1, median_us=2, p95_us=3, result_digest="0123456789abcdef",
               commit=(binary.parent / "revision.txt").read_text().strip(), profile=profile,
               compiler="synthetic", cflags="-O2", opt_level="-O2")
else:
    row = dict(scenario="fresh-result", kind="true", mode=mode, samples=301,
               min_us=1, median_us=2, p95_us=3, workspace_bytes=1024 if mode == "workspace" else 0,
               text_digest="0123456789abcdef", commit=meta["head"], profile=profile,
               compiler="synthetic", cflags="-O2", opt_level="-O2")
rows = ([dict(row, scenario=s, kind=k) for s in ("fresh-result", "alternating-query", "cache-hit")
         for k in ("true", "false")] if kind == "explanations" else [row])
if kind == "sessions":
    rows = [dict(row, policy=p, order=o, values=v, size=s, entries=2048 if s == "maximum" else int(s))
            for p in ("inert", "derive") for o in ("sorted", "reverse", "permuted", "duplicate", "strided")
            for v in ("integer", "symbol") for s in ("8", "16", "31", "32", "33", "64", "128", "256", "402", "maximum")]
with target.open("w", newline="") as stream:
    writer = csv.DictWriter(stream, list(row))
    writer.writeheader()
    writer.writerows(rows)
Path(sys.argv[2]).write_text("{}" if kind == "solver" else "synthetic samples\n")
'''


class OrchestrationTest(unittest.TestCase):
    def test_compile_once_and_all_aa_before_ab(self):
        for enabled in (False, True):
            with self.subTest(explanations=enabled):
                self.exercise(enabled)

    def test_three_revisions_share_one_counterbalanced_campaign(self):
        self.exercise(True, three_way=True)

    def exercise(self, enabled, three_way=False):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            repo = root / "repo"
            (repo / "bench").mkdir(parents=True)
            for name in ("compare_revisions.sh", "compare_runs.py", "compare_explanations.py",
                         "Makefile.compare", "compare_three_revisions.py", "bench_input_edb.c", "bench_explanations.c",
                         "bench_sessions.c", "compare_sessions.py", "diagnose_sessions.py", "report_solver_layout.py"):
                shutil.copy2(ROOT / "bench" / name, repo / "bench" / name)
            (repo / "bench/bench_datalog.c").write_text("synthetic harness\n")
            source = repo / "src/runtime/maelys_datalog_input_edb.c"
            source.parent.mkdir(parents=True)
            source.write_text("linear fixture\n")
            header = repo / "include/maelys/datalog.h"
            header.parent.mkdir(parents=True)
            header.write_text("old API\n")
            def git(*args):
                return subprocess.check_output(["git", "-C", str(repo), *args], text=True).strip()
            git("init", "-q", "-b", "main")
            git("config", "user.name", "Fixture")
            git("config", "user.email", "fixture@example.invalid")
            git("add", ".")
            git("-c", "commit.gpgsign=false", "commit", "-qm", "baseline fixture")
            base = git("rev-parse", "HEAD")
            original = base
            if three_way:
                source.write_text("original candidate fixture\n")
                git("add", ".")
                git("-c", "commit.gpgsign=false", "commit", "-qm", "original fixture")
                original = git("rev-parse", "HEAD")
            source.write_text("indexed fixture\n")
            if enabled:
                header.write_text("maelys_datalog_session_config_set_explanation_workspace(\n")
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
for argument in "$@"; do case "$argument" in OUT=*) output=${argument#OUT=} ;; REVISION=*) revision=${argument#REVISION=} ;; esac; done
mkdir -p "$output"
echo "$revision" > "$output/revision.txt"
cp "$BENCH_TEST_PAYLOAD" "$output/solver"
cp "$BENCH_TEST_PAYLOAD" "$output/input"
cp "$BENCH_TEST_PAYLOAD" "$output/sessions"
for argument in "$@"; do
  if test "$argument" = EXPLANATIONS=1; then cp "$BENCH_TEST_PAYLOAD" "$output/explanations"; fi
done
''',
            }
            for name, body in scripts.items():
                path = bins / name
                path.write_text(body)
                path.chmod(0o755)
            environment = dict(os.environ, PATH=str(bins) + os.pathsep + os.environ["PATH"],
                               TMPDIR=str(root), BENCH_TEST_TRACE=str(trace),
                               BENCH_TEST_PAYLOAD=str(payload))
            if three_way:
                environment["COMPARISON_ORIGINAL"] = original
            output = root / "output"
            result = subprocess.run(["bash", str(repo / "bench/compare_revisions.sh"),
                                     base, "candidate", str(output)],
                                    env=environment, text=True, capture_output=True)
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
            lines = trace.read_text().splitlines()
            if three_way:
                self.assertEqual(sum(line.startswith("make ") for line in lines), 6)
                self.assertTrue(all(line.startswith("make -j1 ") for line in lines[:6]))
                self.assertEqual(len(lines), 130)
                measurements = lines[6:]
                self.assertTrue(all("-aa-" in line for line in measurements[:80]))
                self.assertTrue(all("-ab-" in line for line in measurements[80:]))
                for profile in ("SMALL", "LARGE"):
                    for kind in ("solver", "input", "sessions"):
                        actual = [line.split()[-1].split("-ab-")[-1] for line in measurements
                                  if line.startswith(f"{kind} {profile} ") and "-ab-" in line]
                        self.assertEqual(actual, ["A1", "B1", "C1", "C2", "B2", "A2"])
                for name, a, b in (("base-original", base, original), ("base-revised", base, head),
                                   ("original-revised", original, head)):
                    pair = output / name
                    meta = json.loads((pair / "metadata.json").read_text())
                    self.assertEqual((meta["base"], meta["head"]), (a, b))
                    self.assertIn("indéterminé", (pair / "sessions.md").read_text())
                    self.assertIn("indéterminé", (pair / "comparison.md").read_text())
                    self.assertIn("A B C / C B A", (pair / "sessions.md").read_text())
                    self.assertIn("A B C / C B A", (pair / "comparison.md").read_text())
                self.assertIn("workspace", (output / "base-revised/explanations.md").read_text())
                self.assertTrue((output / "complete").exists())
                return
            self.assertEqual(sum(line.startswith("make ") for line in lines), 4)
            self.assertTrue(all(line.startswith("make -j1 ") for line in lines[:4]))
            count = 68 if enabled else 52
            self.assertEqual(len(lines), count)
            measurements = lines[4:]
            aa_count = 32 if enabled else 24
            self.assertTrue(all("-aa-" in line for line in measurements[:aa_count]))
            self.assertTrue(all("-ab-" in line for line in measurements[aa_count:]))
            for profile in ("SMALL", "LARGE"):
                actual = [line.split()[-1].split("-ab-")[-1] for line in measurements
                          if line.startswith(f"solver {profile} ") and "-ab-" in line]
                self.assertEqual(actual, ["A1", "B1", "A2", "B2"])
                if enabled:
                    actual = [line.split()[-1].split("-ab-")[-1] for line in measurements
                              if line.startswith(f"explanations {profile} ") and "-ab-" in line]
                    self.assertEqual(actual, ["A1", "B1", "A2", "B2"])
                actual = [line.split()[-1].split("-ab-")[-1] for line in measurements
                          if line.startswith(f"sessions {profile} ") and "-ab-" in line]
                self.assertEqual(actual, ["A1", "B1", "A2", "B2"])
            self.assertIn("indéterminé", (output / "sessions.md").read_text())
            if enabled:
                self.assertTrue(all("EXPLANATIONS=0" in line for line in lines[:2]))
                self.assertTrue(all("EXPLANATIONS=1" in line for line in lines[2:4]))
            self.assertIn("workspace" if enabled else "Skipped", (output / "explanations.md").read_text())
            self.assertEqual(json.loads((output / "metadata.json").read_text())["head"], head)
            host = json.loads((output / "metadata.json").read_text())["host"]
            self.assertIsInstance(host["cpu_models"], list)
            self.assertTrue(host["system"])
            for name in ("comparison.md", "sessions.md", "explanations.md"):
                self.assertIn("Measurement CPU:", (output / name).read_text())
            self.assertIn("indéterminé", (output / "comparison.md").read_text())
            self.assertFalse((repo / "bench/results").exists())
            # Existing artifacts must never be overwritten.
            again = subprocess.run(["bash", str(repo / "bench/compare_revisions.sh"),
                                    base, head, str(output)], env=environment, capture_output=True)
            self.assertNotEqual(again.returncode, 0)
            self.assertEqual(len(trace.read_text().splitlines()), count)
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
