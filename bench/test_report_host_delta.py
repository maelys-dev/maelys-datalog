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

class FourVariantEvidence(unittest.TestCase):
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

    def test_variants_and_order_cannot_disappear_from_protocol(self):
        import json
        from report_host_delta import report
        with tempfile.TemporaryDirectory() as d:
            root = Path(d)
            for schema, variants, order in (
                (2, ['A','B','L'], ['A1','B1','L1','L2','B2','A2']),
                (3, ['A','B','L'], ['A1','B1','L1','L2','B2','A2']),
                (3, ['A','B','L','T'], ['A1','B1','L1','T1','L2','T2','B2','A2']),
            ):
                (root/'experiment.json').write_text(json.dumps(dict(schema=schema, variants=variants, order=order, transactions=8)))
                with self.assertRaisesRegex(ValueError, 'schema-3'):
                    report(root)

    def test_all_t_pairs_and_exceptions_retain_exclusive_costs(self):
        from report_host_delta import compare_variants, tombstone_exceptions
        row = dict(profile='LARGE', region='engine/inert/256/integer/one/steady')
        for role, ir in zip(('A','B','L','T'), (100, 60, 80, 70)):
            row[role] = dict(host=[ir,20,10], total=[ir+10,25,15], functions={'compose':[ir,20,10]})
        pairs = compare_variants(row)
        self.assertEqual(set(pairs), {'B/A','L/A','L/B','T/A','T/B','T/L'})
        self.assertEqual(pairs['T/B']['host_saved'][0], -10)
        self.assertEqual(pairs['T/L']['host_saved'][0], 10)
        exceptions = tombstone_exceptions([row])
        self.assertEqual(exceptions[0]['baseline'], 'B')
        self.assertEqual(exceptions[0]['functions'], {'compose':[10,0,0]})
        row['T'] = row['B']
        self.assertEqual(tombstone_exceptions([row]), [])
        del row['T']
        with self.assertRaises(KeyError):
            compare_variants(row)

    def test_loss_and_total_are_preserved_separately(self):
        from report_host_delta import compare
        row = compare(dict(host=[60, 20, 10], total=[100, 40, 30]),
                      dict(host=[80, 15, 12], total=[120, 35, 32]))
        self.assertEqual(row['host_saved'], [-20, 5, -2])
        self.assertEqual(row['total_saved'], [-20, 5, -2])
        self.assertAlmostEqual(row['host_saved_percent'], -100/3)


if __name__ == '__main__':
    unittest.main()
