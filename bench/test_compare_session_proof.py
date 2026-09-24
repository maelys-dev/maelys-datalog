import unittest
import json
from pathlib import Path
import tempfile
from session_proof import CASES, TARGET, PREPARED, annotated, exclusive_dump, strict_verdict, count_report


class SessionProofTests(unittest.TestCase):
    def test_zero_call_arc_is_not_an_exclusive_cost(self):
        profile = """positions: line
events: Ir Dr Dw Bcm I1mr
fn=(1) solve_once_derive_ordered
10 12 3 2 1 0
cfn=(2) child
calls=0 20
10 999 888 777 666 555
fn=(2)
20 5 4 3 2 1
fn=(3) solve_once_derive_ordered'2
30 1 1
"""
        self.assertEqual(exclusive_dump(profile), {
            TARGET: dict(Ir=13, Dr=4, Dw=2, Bcm=1, I1mr=0),
            "child": dict(Ir=5, Dr=4, Dw=3, Bcm=2, I1mr=1)})

    def test_one_extra_event_refuses(self):
        original = dict(Ir=130, Dr=129, Dw=66)
        for event in original:
            changed = dict(original)
            changed[event] += 1
            self.assertTrue(strict_verdict(original, changed, True).startswith("REFUSE"))

    def test_decrease_passes_but_unstable_repeats_refuse(self):
        self.assertEqual(strict_verdict(dict(Ir=130, Dr=129, Dw=66), dict(Ir=129, Dr=128, Dw=65), True), "PASS")
        self.assertTrue(strict_verdict(dict(Ir=130, Dr=129, Dw=66), dict(Ir=130, Dr=129, Dw=66), False).startswith("REFUSE"))

    def test_parse_five_exclusive_events_and_merge_inline_rows(self):
        text = " 2,797 ( 0.18%) 129 ( 1.00%) 66 ( 2.00%) . 4 ( 0.01%) /path/file.c:solve_once_derive_ordered [binary]\n"
        text += " 1 2 3 4 5 /other/file.c:solve_once_derive_ordered'2 [binary]\n"
        self.assertEqual(annotated(text), {"solve_once_derive_ordered": dict(Ir=2798, Dr=131, Dw=69, Bcm=4, I1mr=9)})
        with self.assertRaises(ValueError):
            annotated("Ir Dr Dw Bcm I1mr PROGRAM TOTALS\n")

    def test_full_inventory_reports_cache_separately_and_refuses_one_unit(self):
        costs = {}
        for profile in ("SMALL", "LARGE"):
            for role in ("A", "B"):
                for repeat in (1, 2):
                    for key in CASES:
                        costs[profile, role, repeat, key] = {
                            TARGET: dict(Ir=100, Dr=30, Dw=20, Bcm=repeat, I1mr=3),
                            PREPARED: dict(Ir=41 if role == "A" else 49, Dr=10, Dw=5 if role == "A" else 7, Bcm=0, I1mr=0)}
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            self.assertEqual(count_report(root, costs), ([], []))
            self.assertEqual(len(json.loads((root / "counts.json").read_text())), 400)
            key = sorted(CASES)[0]
            for repeat in (1, 2):
                costs["SMALL", "B", repeat, key][TARGET]["Dw"] += 1
            failures, unexpected = count_report(root, costs)
            self.assertEqual(len(failures), 1)
            self.assertEqual(len(unexpected), 1)


if __name__ == "__main__":
    unittest.main()
