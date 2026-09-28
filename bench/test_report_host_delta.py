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

class ThreeVariantEvidence(unittest.TestCase):
    def make_run(self, root):
        directory = root/'A1-engine'
        directory.mkdir()
        stem = 'A/engine/inert/8/integer/empty'
        for i, phase in enumerate(('init', 'first', 'steady')):
            (directory/f'counts.{i}').write_text(ExclusiveCounts.sample.replace(stem+'/steady', stem+'/'+phase))
        header = 'case,transactions,digest,retained_bytes,bank_bytes,final_unique_facts,setup_calls,setup_bytes\n'
        row = stem+',8,123,36888,225408,8,11,1498976\n'
        (directory/'receipts.csv').write_text(header+row)
        return directory, header, row

    def test_identity_and_missing_evidence_are_not_silently_accepted(self):
        from report_host_delta import load_run
        for failure in ('missing_region', 'duplicate_region', 'duplicate_receipt', 'wrong_role', 'wrong_length'):
            with self.subTest(failure=failure), tempfile.TemporaryDirectory() as d:
                directory, header, row = self.make_run(Path(d))
                self.assertEqual(len(load_run(directory)[0]), 3)
                if failure == 'missing_region':
                    (directory/'counts.2').unlink()
                elif failure == 'duplicate_region':
                    (directory/'counts.3').write_text((directory/'counts.2').read_text())
                elif failure == 'duplicate_receipt':
                    (directory/'receipts.csv').write_text(header+row+row)
                elif failure == 'wrong_role':
                    p = directory/'counts.2'
                    p.write_text(p.read_text().replace('A/engine', 'L/engine'))
                else:
                    (directory/'receipts.csv').write_text(header+row.replace(',8,123,', ',32,123,'))
                with self.assertRaises(ValueError):
                    load_run(directory)

    def test_linear_cannot_disappear_from_protocol(self):
        import json
        from report_host_delta import report
        with tempfile.TemporaryDirectory() as d:
            root = Path(d)
            (root/'experiment.json').write_text(json.dumps(dict(schema=2, variants=['A','B'], order=['A1','B1','B2','A2'], transactions=8)))
            with self.assertRaises(ValueError):
                report(root)

    def test_loss_and_total_are_preserved_separately(self):
        from report_host_delta import compare
        row = compare(dict(host=[60, 20, 10], total=[100, 40, 30]),
                      dict(host=[80, 15, 12], total=[120, 35, 32]))
        self.assertEqual(row['host_saved'], [-20, 5, -2])
        self.assertEqual(row['total_saved'], [-20, 5, -2])
        self.assertAlmostEqual(row['host_saved_percent'], -100/3)


if __name__ == '__main__':
    unittest.main()
