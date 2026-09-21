# SPDX-License-Identifier: MPL-2.0
import contextlib
import csv
import io
from pathlib import Path
import tempfile
import unittest
from unittest.mock import patch
from diagnose_sessions import residuals, run, verify_count
import test_compare_sessions as fixtures


class SessionDiagnosticsTest(unittest.TestCase):
    def fixture(self, root):
        fixtures.SessionComparisonTest().fixture(root)
        for p in (1, 2):
            path = root / f"SMALL-sessions-ab-B{p}.csv"
            with path.open(newline="") as f: rows = list(csv.DictReader(f))
            for row in rows:
                # Keep one 5% slowdown and count only the distinct >9.94% case.
                if (row["policy"], row["order"], row["values"]) == ("inert", "sorted", "integer"):
                    if row["size"] in ("31", "33"):
                        ratio = 1.05 if row["size"] == "31" else 1.20
                        row.update(min_us=15*ratio, median_us=20*ratio, p95_us=30*ratio)
            with path.open("w", newline="") as f:
                w = csv.DictWriter(f, rows[0]); w.writeheader(); w.writerows(rows)

    def test_only_above_reference_is_counted_and_oracle_is_required(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary); self.fixture(root)
            meta, rows = residuals(root)
            self.assertEqual(len(rows), 4)
            self.assertEqual(sum(r["above_reference"] for r in rows), 2)
            calls = []
            for role in ("A", "B"):
                binary = root / f"bin-{role}-SMALL/sessions-counts"
                binary.parent.mkdir(); binary.write_bytes(b"synthetic binary")
            def fake_run(command, **kwargs):
                calls.append(command)
                if command[0] == "valgrind":
                    out = Path(next(c.split("=", 1)[1] for c in command if c.startswith("--callgrind-out-file=")))
                    binary, summary, raw = map(Path, command[5:8])
                    key = command[8:]
                    role = binary.parent.name.split("-")[1]
                    out.write_text("events: Ir\nsummary: 10000\n")
                    row = dict(zip(("policy", "order", "values", "size"), key), profile="SMALL",
                               commit=meta["base" if role == "A" else "head"], samples="1",
                               result_digest="0123456789abcdef", min_us="0.000000", median_us="0.000000", p95_us="0.000000")
                    for path, data in ((summary, row), (raw, dict(zip(("policy", "order", "values", "size"), key), sample="0", elapsed_us="0.000000"))):
                        with path.open("w", newline="") as f:
                            w = csv.DictWriter(f, data); w.writeheader(); w.writerow(data)
                else:
                    kwargs["stdout"].write("10,000 (100.0%) src/edb.c:maelys_datalog_edb_add_fact [binary]\n")
            output = io.StringIO()
            with patch("diagnose_sessions.subprocess.check_output", return_value="synthetic valgrind\n"), patch("diagnose_sessions.subprocess.run", side_effect=fake_run), contextlib.redirect_stdout(output):
                run(root, root)
            self.assertEqual(sum(c[0] == "valgrind" for c in calls), 4)
            self.assertTrue(all(c[-1] == "33" for c in calls if c[0] == "valgrind"))
            self.assertIn("1 distinct cases selected", output.getvalue())
            self.assertIn("| no |", output.getvalue())
            prefix = root / "session-counts/000-SMALL-A-1"
            path = prefix.with_suffix(".csv")
            path.write_text(path.read_text().replace("0123456789abcdef", "ffffffffffffffff"))
            with self.assertRaisesRegex(ValueError, "oracle"):
                verify_count(prefix, "SMALL", meta["base"], ("inert", "sorted", "integer", "33"), "0123456789abcdef")

    def test_no_callgrind_for_no_residuals(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary); fixtures.SessionComparisonTest().fixture(root)
            with patch("diagnose_sessions.subprocess.run") as child, contextlib.redirect_stdout(io.StringIO()):
                run(root, root)
            child.assert_not_called()
