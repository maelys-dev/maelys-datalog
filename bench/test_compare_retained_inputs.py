#!/usr/bin/env python3
"""Synthetic evidence tests; not benchmark results."""
import csv
import json
import tempfile
import unittest
from pathlib import Path
from compare_retained_inputs import CASES, ORDER, DELIVERY_ORDER, report

class RetainedReport(unittest.TestCase):
    def fixture(self, root, delivery=False):
        if delivery:(root/"manifest.json").write_text(json.dumps({"backend_delivery":True}))
        for profile in ("SMALL", "LARGE"):
            for label in (DELIVERY_ORDER if delivery else ORDER):
                path=root/profile/label;path.mkdir(parents=True)
                (path/"counts").write_text("desc: Trigger: Program termination\nsummary: 0\ntotals: 0\n")
                with (path/"receipts.csv").open("w") as stream:
                    writer=csv.writer(stream);writer.writerow(("case","transactions","digest"))
                    for index,case in enumerate(CASES):
                        key=f"{label[:-1]}/{case}"
                        writer.writerow((key,200,"abcdef"))
                        (path/f"counts.{index}").write_text(f"desc: Trigger: Client Request: {key}\nevents: Ir Dr Dw\nsummary: 12 3 4\nfn=(1) host\n1 10 2 3\ntotals: 10 2 3\n")

    def test_complete(self):
        with tempfile.TemporaryDirectory() as directory:
            root=Path(directory);self.fixture(root);r=report(root)
            self.assertEqual(r["repeated_regions"],192)
            self.assertEqual(len(r["comparisons"]),64)
            self.assertEqual(r["comparisons"][0]["paths"]["delta"]["residual"],[2,1,1])

    def test_missing_and_changed(self):
        with tempfile.TemporaryDirectory() as directory:
            root=Path(directory);self.fixture(root)
            file=root/"SMALL/delta2/counts.0";original=file.read_text()
            file.unlink()
            with self.assertRaisesRegex(ValueError,"missing regions"):report(root)
            file.write_text(original.replace("12 3 4","13 3 4"))
            with self.assertRaisesRegex(ValueError,"nonidentical repetition"):report(root)
            file.write_text(original)
            receipts=root/"LARGE/replace2/receipts.csv"
            receipts.write_text(receipts.read_text().replace("abcdef","changed",1))
            with self.assertRaisesRegex(ValueError,"nonidentical repetition"):report(root)

    def test_delivery(self):
        with tempfile.TemporaryDirectory() as directory:
            root=Path(directory);self.fixture(root,True);r=report(root)
            self.assertEqual(r["reference_role"],"snapshot6")
            self.assertEqual(r["comparisons"][0]["paths"]["delta7"]["provider_exclusive"],[0,0,0])
            self.assertEqual(r["comparisons"][0]["paths"]["delta7"]["host_shared_and_driver"],[10,2,3])
            self.assertEqual(r["comparisons"][0]["paths"]["delta7"]["vs_delta6_percent"],[0,0,0])

if __name__=="__main__":unittest.main()
