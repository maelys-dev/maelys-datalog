# SPDX-License-Identifier: MPL-2.0
import contextlib
import csv
import io
import json
from pathlib import Path
import tempfile
import unittest

from compare_runs import INPUT_KEY, METRICS, PASSES, SOLVER_KEY, compare, load, report


class ComparisonTest(unittest.TestCase):
    def fixture(self, directory):
        (directory / "metadata.json").write_text(json.dumps({
            "base": "a" * 40, "head": "b" * 40,
            "input_implementation_identical": False,
        }))
        for profile in ("SMALL", "LARGE"):
            for kind in ("solver", "input"):
                for name in PASSES:
                    fields = list(SOLVER_KEY if kind == "solver" else INPUT_KEY) + ["samples"] + list(METRICS)
                    fields += ["commit", "opt_level", "compiler", "cflags"] if kind == "solver" else [
                        "reserved_bytes", "clear_min_us", "clear_median_us", "clear_p95_us"]
                    with (directory / f"{profile}-{kind}-{name}.csv").open("w", newline="") as stream:
                        writer = csv.DictWriter(stream, fields)
                        writer.writeheader()
                        for micro in (True, False):
                            candidate = name.startswith("ab-B")
                            # Micro median reports +80%; minimum only reports +1%,
                            # under the 2% observed A/A floor. Must be indéterminé.
                            minimum = (1.01 if candidate else 1.02 if name == "aa-2" else 1) if micro else 10
                            median = (9 if candidate else 5) if micro else (22 if candidate else 20)
                            row = dict(samples=1000 if kind == "solver" else 301,
                                       min_us=minimum, median_us=median,
                                       p95_us=(9 if micro else 26) if candidate else (6 if micro else 25))
                            if kind == "solver":
                                row.update(benchmark="micro" if micro else "macro", group="g",
                                           mode="m", size=1 if micro else 2, selectivity=1,
                                           commit=("b" if candidate else "a") * 40,
                                           opt_level="-O2", compiler="clang", cflags="-O2")
                            else:
                                row.update(scenario="crossover", capacity=13 if micro else 26,
                                           entries=13 if micro else 26, distinct_strings=64 if micro else 128,
                                           mode="batch", text_capacity=384 if micro else 768,
                                           reserved_bytes=2048 if candidate else 1024,
                                           clear_min_us=0.01, clear_median_us=0.02, clear_p95_us=0.03)
                            writer.writerow(row)

    def test_noise_floor_minimum_and_p95(self):
        with tempfile.TemporaryDirectory() as temporary:
            directory = Path(temporary)
            self.fixture(directory)
            output = io.StringIO()
            with contextlib.redirect_stdout(output):
                report(directory)
            text = output.getvalue()
            self.assertIn("| min_us | 1.000000 | 1.010000 | 1.0100 | 2.00% | indéterminé |", text)
            self.assertIn("| median_us | 20.000000 | 22.000000 | 1.1000 | 0.00% | slower |", text)
            self.assertIn("| p95_us | 25.000000 | 26.000000 | 1.0400 | 0.00% | slower |", text)
            self.assertIn("dedicated machine", text)
            self.assertNotIn("unmeasurable", text)
            self.assertEqual(text.count("## SMALL /"), 2)
            self.assertEqual(text.count("## LARGE /"), 2)

    def test_floor_is_symmetric_and_boundary_indeterminate(self):
        rows = [{"min_us": x} for x in (100, 110, 100, 100, 100, 100, 110, 110)]
        self.assertEqual(compare(rows[:4], rows[4:6], rows[6:], "min_us")[-1], "indéterminé")
        rows = [{"min_us": x} for x in (110, 100, 100, 100, 110, 110, 100, 100)]
        self.assertEqual(compare(rows[:4], rows[4:6], rows[6:], "min_us")[-1], "indéterminé")

    def test_zero_clear_is_indeterminate_not_a_speedup(self):
        aa = [{"clear_min_us": 0}] * 4
        ab = [{"clear_min_us": 1}] * 2
        self.assertIn("indéterminé", compare(aa, ab, ab, "clear_min_us")[-1])

    def test_reject_missing_duplicate_nonfinite_or_unordered_rows(self):
        with tempfile.TemporaryDirectory() as temporary:
            directory = Path(temporary)
            self.fixture(directory)
            path = directory / "SMALL-solver-aa-1.csv"
            original = path.read_text()
            path.write_text("benchmark\n")
            with self.assertRaisesRegex(ValueError, "missing"):
                load(path, "solver")
            path.write_text(original + original.splitlines()[1] + "\n")
            with self.assertRaisesRegex(ValueError, "duplicate"):
                load(path, "solver")
            for value in ("nan", "inf", "-1", "99"):
                lines = original.splitlines()
                fields = lines[1].split(",")
                fields[6] = value  # min_us
                lines[1] = ",".join(fields)
                path.write_text("\n".join(lines) + "\n")
                with self.assertRaises(ValueError):
                    load(path, "solver")

    def test_reject_missing_case_and_wrong_commit(self):
        with tempfile.TemporaryDirectory() as temporary:
            directory = Path(temporary)
            self.fixture(directory)
            path = directory / "SMALL-solver-ab-B1.csv"
            original = path.read_text()
            path.write_text("\n".join(original.splitlines()[:2]) + "\n")
            with contextlib.redirect_stdout(io.StringIO()), self.assertRaisesRegex(ValueError, "inventory"):
                report(directory)
            path.write_text(original.replace("b" * 40, "a" * 40))
            with contextlib.redirect_stdout(io.StringIO()), self.assertRaisesRegex(ValueError, "revision"):
                report(directory)

    def test_identical_input_cannot_establish_threshold(self):
        with tempfile.TemporaryDirectory() as temporary:
            directory = Path(temporary)
            self.fixture(directory)
            path = directory / "metadata.json"
            value = json.loads(path.read_text())
            value["input_implementation_identical"] = True
            path.write_text(json.dumps(value))
            output = io.StringIO()
            with contextlib.redirect_stdout(output):
                report(directory)
            self.assertIn("cannot establish a linear/indexed crossover", output.getvalue())


if __name__ == "__main__":
    unittest.main()
