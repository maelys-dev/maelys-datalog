# SPDX-License-Identifier: MPL-2.0
import tempfile
import unittest
from pathlib import Path
from report_host_delta import counts, category, vector


class ExclusiveCounts(unittest.TestCase):
    sample = '''desc: Trigger: Client Request: A/engine/inert/8/integer/empty/steady
positions: line
events: Ir Dr Dw
summary: 100 40 30
fn=(1) delta_backend_solve
1 30 10 5
cfn=(2) maelys_datalog_backend_emit
calls=1 1
* 40 20 15
fn=(2)
1 40 20 15
fn=(3) delta_execute
1 20 5 5
totals: 90 35 25
'''

    def parse(self, source):
        with tempfile.TemporaryDirectory() as d:
            path = Path(d)/"counts.1"
            path.write_text(source)
            return counts(path)

    def test_inclusive_services_are_not_backend_work(self):
        row = self.parse(self.sample)
        self.assertEqual(row["residual"], [10, 5, 5])
        self.assertEqual(vector(row["functions"], lambda n: category(n)=="backend_exclusive"), [30, 10, 5])
        self.assertEqual(vector(row["functions"], lambda n: category(n)!="backend_exclusive"), [60, 25, 20])
        self.assertEqual(category("maelys_datalog_backend_emit"), "host_output_services")

    def test_missing_or_inconsistent_counts_reject(self):
        for source in (self.sample.replace("totals: 90", "totals: 89"),
                       self.sample.replace("summary: 100", "summary: 80"),
                       self.sample.replace("events: Ir Dr Dw", "events: Dw Ir Dr"),
                       self.sample.replace("summary: 100 40 30", "summary: 100")):
            with self.assertRaises(ValueError):
                self.parse(source)

    def test_final_dump_is_not_an_operation(self):
        self.assertIsNone(self.parse(self.sample.replace("desc: Trigger: Client Request: A/engine/inert/8/integer/empty/steady", "desc: Trigger: Program termination")))


if __name__ == '__main__':
    unittest.main()
