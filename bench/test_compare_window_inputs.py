#!/usr/bin/env python3
"""Synthetic completeness and rejection witnesses; not measurements."""
import csv
import tempfile
import unittest
from pathlib import Path
from compare_window_inputs import CASES, ORDER, report

class WindowReport(unittest.TestCase):
    def fixture(self,root):
        for profile in ('SMALL','LARGE'):
            for label in ORDER:
                path=root/profile/label;path.mkdir(parents=True)
                with (path/'receipts.csv').open('w') as stream:
                    writer=csv.writer(stream);writer.writerow(('case','transactions','digest'))
                    for i,case in enumerate(CASES):
                        key=f'{label[:-1]}/{case}';writer.writerow((key,200,'abcdef'))
                        (path/f'counts.{i}').write_text(f'desc: Trigger: Client Request: {key}\nevents: Ir Dr Dw\nsummary: 12 4 5\nfn=(1) host\n1 8 2 2\nfn=(2) input_fixture_derive\n1 2 1 1\ntotals: 10 3 3\n')
    def test_complete(self):
        with tempfile.TemporaryDirectory() as d:
            root=Path(d);self.fixture(root);r=report(root)
            self.assertEqual((r['regions'],r['repeated_regions'],len(r['comparisons'])),(384,192,96))
            p=r['comparisons'][0]['paths']['head']
            self.assertEqual(p['residual'],[2,1,2]);self.assertEqual(p['provider_exclusive'],[2,1,1]);self.assertEqual(p['host_shared_and_driver'],[8,2,2])
    def test_missing_changed_duplicate(self):
        with tempfile.TemporaryDirectory() as d:
            root=Path(d);self.fixture(root);p=root/'SMALL/head2/counts.0';original=p.read_text();p.unlink()
            with self.assertRaisesRegex(ValueError,'missing regions'):report(root)
            p.write_text(original.replace('12 4 5','13 4 5'))
            with self.assertRaisesRegex(ValueError,'nonidentical repetition'):report(root)
            p.write_text(original);p.with_name('counts.duplicate').write_text(original)
            with self.assertRaisesRegex(ValueError,'duplicate region'):report(root)
    def test_outputs_and_steps(self):
        with tempfile.TemporaryDirectory() as d:
            root=Path(d);self.fixture(root)
            for label in ('head1','head2'):
                p=root/'LARGE'/label/'receipts.csv';p.write_text(p.read_text().replace('abcdef','wrong',1))
            with self.assertRaisesRegex(ValueError,'outputs differ'):report(root)
            p=root/'SMALL/base1/receipts.csv';p.write_text(p.read_text().replace(',200,',',199,',1))
            with self.assertRaisesRegex(ValueError,'invalid receipts'):report(root)
if __name__=='__main__':unittest.main()
