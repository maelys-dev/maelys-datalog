# SPDX-License-Identifier: MPL-2.0
import contextlib
import csv
import io
import json
from pathlib import Path
import tempfile
import unittest
from compare_sessions import CASES, PASSES, load, report


class SessionComparisonTest(unittest.TestCase):
    def fixture(self, root):
        (root / "metadata.json").write_text(json.dumps({"base": "a" * 40, "head": "b" * 40}))
        for profile in ("SMALL", "LARGE"):
            for name in PASSES:
                rows = []
                candidate = name.startswith("ab-B")
                for policy, order, values, size in sorted(CASES):
                    micro = size == "8"
                    scale = .5 if candidate else 1.02 if name == "aa-2" else 1
                    row = dict(policy=policy, order=order, values=values, size=size,
                               entries=2048 if size == "maximum" else int(size), edb_limit=2048,
                               samples=301, min_us=(1 if micro else 15) * scale,
                               median_us=(2 if micro else 20) * scale, p95_us=(3 if micro else 30) * scale,
                               result_digest="0123456789abcdef", commit=("b" if candidate else "a") * 40,
                               profile=profile, compiler="clang", cflags="-O2", opt_level="-O2")
                    rows.append(row)
                with (root / f"{profile}-sessions-{name}.csv").open("w", newline="") as f:
                    writer = csv.DictWriter(f, rows[0].keys()); writer.writeheader(); writer.writerows(rows)

    def test_complete_report_and_noise_selection(self):
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp); self.fixture(root)
            output = io.StringIO()
            with contextlib.redirect_stdout(output): report(root)
            self.assertIn("| min_us | 1.000000 | 0.500000 | 0.5000 | 2.00% | faster |", output.getvalue())
            self.assertIn("| p95_us | 30.000000 | 15.000000 | 0.5000 | 2.00% | faster |", output.getvalue())
            self.assertIn("does not measure pure derivation", output.getvalue())

    def test_incomplete_and_corrupt_evidence(self):
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp); self.fixture(root)
            path = root / "SMALL-sessions-ab-B1.csv"
            with path.open(newline="") as f:
                rows = list(csv.DictReader(f))
            def write(changed):
                with path.open("w", newline="") as f:
                    writer = csv.DictWriter(f, rows[0].keys()); writer.writeheader(); writer.writerows(changed)
            for field, value, reason in (("commit", "a" * 40, "revision"), ("min_us", "nan", "timings"),
                                         ("entries", "0", "capacity"), ("result_digest", "bad", "digest"),
                                         ("result_digest", "f" * 16, "result mismatch"),
                                         ("compiler", "another", "compiler")):
                with self.subTest(field=field, value=value):
                    changed = [dict(r) for r in rows]; changed[0][field] = value; write(changed)
                    with contextlib.redirect_stdout(io.StringIO()), self.assertRaisesRegex(ValueError, reason): report(root)
            write(rows[:-1])
            with self.assertRaisesRegex(ValueError, "inventory"): load(path, "SMALL", "b" * 40)
            write(rows + [rows[0]])
            with self.assertRaisesRegex(ValueError, "duplicate"): load(path, "SMALL", "b" * 40)

    def test_consistently_wrong_permutation_is_rejected(self):
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp); self.fixture(root)
            for name in PASSES:
                path = root / f"SMALL-sessions-{name}.csv"
                with path.open(newline="") as f: rows = list(csv.DictReader(f))
                for row in rows:
                    if row["order"] == "reverse": row["result_digest"] = "f" * 16
                with path.open("w", newline="") as f:
                    writer = csv.DictWriter(f, rows[0].keys()); writer.writeheader(); writer.writerows(rows)
            with contextlib.redirect_stdout(io.StringIO()), self.assertRaisesRegex(ValueError, "permutations"): report(root)


if __name__ == "__main__": unittest.main()
