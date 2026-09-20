# SPDX-License-Identifier: MPL-2.0
import contextlib
import csv
import io
import json
from pathlib import Path
import tempfile
import unittest

from compare_explanations import CASES, PASSES, report


class ExplanationEvidenceTest(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)
        (self.root / "metadata.json").write_text(json.dumps({"head": "candidate"}))
        (self.root / "explanations-enabled.txt").write_text("1\n")
        for profile in ("SMALL", "LARGE"):
            for name in PASSES:
                mode = "workspace" if name.startswith("ab-B") else "legacy"
                rows = [dict(scenario=s, kind=k, mode=mode, samples=301,
                             min_us=1, median_us=20, p95_us=30,
                             workspace_bytes=4096 if mode == "workspace" else 0,
                             text_digest="0123456789abcdef", commit="candidate", profile=profile,
                             compiler="clang", cflags="-O2", opt_level="-O2") for s, k in sorted(CASES)]
                self.write(profile, name, rows)

    def write(self, profile, name, rows):
        with (self.root / f"{profile}-explanations-{name}.csv").open("w", newline="") as stream:
            writer = csv.DictWriter(stream, list(rows[0]))
            writer.writeheader()
            writer.writerows(rows)

    def output(self):
        buffer = io.StringIO()
        with contextlib.redirect_stdout(buffer):
            report(self.root)
        return buffer.getvalue()

    def test_equal_timings_are_indeterminate_with_separate_tails(self):
        output = self.output()
        self.assertIn("24/24 comparisons are indéterminé", output)
        self.assertIn("median_us", output)
        self.assertIn("p95_us", output)
        self.assertNotIn("| min_us |", output)

    def test_bad_evidence_cannot_produce_a_report(self):
        path = self.root / "SMALL-explanations-ab-B1.csv"
        with path.open(newline="") as stream:
            original = list(csv.DictReader(stream))
        for field, value in (("mode", "legacy"), ("commit", "baseline"), ("profile", "LARGE"),
                             ("samples", "300"), ("min_us", "nan"), ("p95_us", "0"),
                             ("text_digest", "fedcba9876543210"), ("workspace_bytes", "8192"),
                             ("compiler", "different-clang")):
            with self.subTest(field=field):
                rows = [dict(row) for row in original]
                rows[0][field] = value
                self.write("SMALL", "ab-B1", rows)
                with self.assertRaises(ValueError):
                    self.output()
        for rows in (original[:-1], original + [original[0]]):
            self.write("SMALL", "ab-B1", rows)
            with self.assertRaises(ValueError):
                self.output()

    def test_skip_is_explicit_and_missing_pass_fails(self):
        (self.root / "SMALL-explanations-ab-B1.csv").unlink()
        with self.assertRaises(OSError):
            self.output()
        (self.root / "explanations-enabled.txt").write_text("0\n")
        self.assertIn("Skipped", self.output())


if __name__ == "__main__":
    unittest.main()
