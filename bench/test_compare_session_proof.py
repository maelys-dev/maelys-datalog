import unittest
from session_proof import annotated, strict_verdict


class SessionProofTests(unittest.TestCase):
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


if __name__ == "__main__":
    unittest.main()
