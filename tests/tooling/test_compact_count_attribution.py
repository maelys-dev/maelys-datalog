# SPDX-License-Identifier: MPL-2.0
"""Two static functions with one name must not share software costs."""
import importlib.util
from pathlib import Path
import tempfile
import unittest
ROOT = Path(__file__).resolve().parents[2]
spec = importlib.util.spec_from_file_location('compact_report', ROOT/'bench/report_host_delta.py')
report = importlib.util.module_from_spec(spec); spec.loader.exec_module(report)

class Attribution(unittest.TestCase):
    def test_static_names_and_inclusive_edges(self):
        raw = '''desc: Trigger: Client Request: compact
events: Ir Dr Dw
summary: 30 6 9
ob=(1) /out/head/compact-storage
fl=(1) /out/head-source/src/core/a.c
fn=(1) validate_fact
1 10 2 3
cfl=(2) /out/head-source/src/runtime/b.c
cfn=(2) validate_fact
calls=1 1
1 20 4 6
fl=(2)
fn=(2)
1 20 4 6
totals: 30 6 9
'''
        with tempfile.TemporaryDirectory() as d:
            p=Path(d)/'counts';p.write_text(raw)
            old=report.counts(p);split=report.counts(p,separate_sources=True)
        self.assertEqual(old['functions'],{'validate_fact':[30,6,9]})
        self.assertEqual(split['functions'],{
            'compact-storage|src/core/a.c|validate_fact':[10,2,3],
            'compact-storage|src/runtime/b.c|validate_fact':[20,4,6]})
        self.assertEqual(old['total'],split['total'])
        self.assertEqual(split['residual'],[0,0,0])

if __name__=='__main__':unittest.main()
