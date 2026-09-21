# SPDX-License-Identifier: MPL-2.0
import contextlib
import csv
import io
from pathlib import Path
import tempfile
import unittest
from report_solver_layout import PADS, ROLES, address, data_layout, fixture_size, functions, instructions, report, timing


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
            self.assertIn("| B/0 → C/0 | median_us |", out.getvalue())
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

    def write_layout(self, path, base=32):
        values = {"sizeof.ruleset": 351960, "alignof.ruleset": 8,
                  "address_mod64.ruleset": base}
        for field, offset in dict(strata=4912, symbols=5440, registry=46416,
                                  facts=73064, rules=82288).items():
            values["offsetof.ruleset." + field] = offset
            values["address_mod64.ruleset." + field] = (base + offset) % 64
        with path.open("w", newline="") as stream:
            writer = csv.writer(stream); writer.writerow(["key", "value"])
            writer.writerows(values.items())

    def test_data_alignment_and_invalid_layouts(self):
        with tempfile.TemporaryDirectory() as tmp:
            path = Path(tmp) / "layout.csv"
            self.write_layout(path)
            values = data_layout(path)
            self.assertEqual(values["offsetof.ruleset.symbols"] % 64, 0)
            self.assertEqual(values["address_mod64.ruleset.symbols"], 32)
            valid = path.read_text()
            path.write_text(valid.replace("address_mod64.ruleset.symbols,32", "address_mod64.ruleset.symbols,0"))
            with self.assertRaisesRegex(ValueError, "inconsistent"): data_layout(path)
            path.write_text(valid + "sizeof.ruleset,351960\n")
            with self.assertRaisesRegex(ValueError, "duplicate"): data_layout(path)
            path.write_text(valid.replace("alignof.ruleset,8\n", ""))
            with self.assertRaisesRegex(ValueError, "missing"): data_layout(path)

    def test_selectable_case_and_layout_inventory(self):
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp); self.fixture(root)
            revisions = root / "revisions.txt"
            self.assertEqual(fixture_size(root), "2048")
            revisions.write_text(revisions.read_text() + "case=solver_size_pure/1024\n")
            for role in ROLES:
                for pad in PADS: self.write_layout(root / f"{role}-{pad}.layout.csv")
            out = io.StringIO()
            with contextlib.redirect_stdout(out): report(root)
            self.assertIn("solver_size_pure / 1024", out.getvalue())
            self.assertIn("| C | 256 | 351960 | 5440 | 32 | 32 |", out.getvalue())
            (root / "C-256.layout.csv").unlink()
            with contextlib.redirect_stdout(io.StringIO()), self.assertRaisesRegex(ValueError, "inventory"):
                report(root)
            revisions.write_text("case=solver_size_pure/512\n")
            with self.assertRaisesRegex(ValueError, "case"): fixture_size(root)
