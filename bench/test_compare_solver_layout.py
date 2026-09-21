# SPDX-License-Identifier: MPL-2.0
import contextlib
import csv
import io
from pathlib import Path
import tempfile
import unittest
from report_solver_layout import PADS, ROLES, address, functions, instructions, report, timing


class SolverLayoutTest(unittest.TestCase):
    def fixture(self, root):
        (root / "revisions.txt").write_text("base=a\noriginal=b\nhead=c\n")
        def samples(path, count=1000, elapsed="10.000000"):
            with path.open("w", newline="") as stream:
                writer = csv.DictWriter(stream, ["sample", "elapsed_us", "result"])
                writer.writeheader()
                writer.writerows(dict(sample=i, elapsed_us=elapsed, result=17) for i in range(count))
        for role in ROLES:
            for p in (1, 2, 3, 4): samples(root / f"{role}-aa-{p}.csv")
            for pad in PADS:
                (root / f"{role}-{pad}.nm").write_text(
                    f"{4096+pad:016x} 00000020 T maelys_datalog_edb_add_fact\n"
                    f"{8192+pad:016x} 00000020 T maelys_datalog_solve_once\n")
                for p in (1, 2):
                    samples(root / f"{role}-{pad}-{p}.csv", elapsed="9.000000" if pad else "10.000000")
                    samples(root / f"ir-{role}-{pad}-{p}.csv", 1, "0.000000")
                    (root / f"ir-{role}-{pad}-{p}.out").write_text("events: Ir\nsummary: 10000\n")
                    (root / f"ir-{role}-{pad}-{p}.functions.txt").write_text(
                        "10,000 (100.0%)  /src/solver.c:solve_once [binary]\n")

    def test_report_and_fail_closed(self):
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp); self.fixture(root)
            out = io.StringIO()
            with contextlib.redirect_stdout(out): report(root)
            self.assertIn("| B/0 → B/256 | median_us |", out.getvalue())
            self.assertIn("No exclusive function-count difference", out.getvalue())
            self.assertIn("does not", out.getvalue())
            path = root / "B-16.nm"
            path.write_text(path.read_text().replace("0000000000001010", "0000000000001000"))
            with contextlib.redirect_stdout(io.StringIO()), self.assertRaisesRegex(ValueError, "placement"):
                report(root)

    def test_invalid_counts_and_timing(self):
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp); self.fixture(root)
            path = root / "ir-A-0-1.out"
            path.write_text("events: Ir Dr\nsummary: 10 20\n")
            with self.assertRaisesRegex(ValueError, "events"): instructions(path)
            path.write_text("10,000 (100.0%)  edb.c:maelys_datalog_edb_add_fact [binary]\n")
            with self.assertRaisesRegex(ValueError, "preparation"): functions(path)
            path = root / "A-0-1.csv"
            path.write_text(path.read_text().replace("10.000000", "nan", 1))
            with self.assertRaisesRegex(ValueError, "timing"): timing(path)
            path.write_text("sample,elapsed_us,result\n0,10,17\n")
            with self.assertRaisesRegex(ValueError, "inventory"): timing(path)
