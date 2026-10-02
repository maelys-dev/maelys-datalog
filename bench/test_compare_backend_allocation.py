# SPDX-License-Identifier: MPL-2.0
import csv
import json
from pathlib import Path
import tempfile
import unittest
from unittest.mock import patch
import compare_backend_allocation as tool

class Evidence(unittest.TestCase):
    def setUp(self):
        self.tmp=tempfile.TemporaryDirectory();self.addCleanup(self.tmp.cleanup)
        self.root=Path(self.tmp.name)
        self.addCleanup(patch.stopall)
        patch.object(tool,'counts',lambda p:json.loads(p.read_text())).start()
        for profile in tool.PROFILES:
            for label in tool.ORDER:
                self.run_fixture(profile,label,tool.FIXED)
            for label in ('elastic1','elastic2','bytes'):
                self.run_fixture(profile,label,tool.ELASTIC)
    def run_fixture(self,profile,label,cases):
        path=self.root/profile/label;path.mkdir(parents=True)
        rows=[]
        for i,case in enumerate(cases):
            r=dict(case='role/'+case,transactions='200',digest='1')
            if case.startswith('elastic'):
                r.update(fixed_bytes='100',current_before='200',current_after='200',operation_peak='300',acquire_calls='600',release_calls='600',copy_requests='0',move_requests='0',set_requests='0',banks='1',cap_per_bank='1000',configured_bound='1000')
                if label=='bytes':r['copy_requests']='64'
            rows.append(r)
            counts=dict(label='role/'+case,total=[100,50,25],functions={'allocation_fixture_solve':[40,20,10],'allocation_acquire':[20,10,5],'host':[40,20,10]},residual=[0,0,0])
            (path/f'counts.{i}').write_text(json.dumps(counts))
        with (path/'receipts.csv').open('w') as f:
            w=csv.DictWriter(f,fieldnames=rows[0]);w.writeheader();w.writerows(rows)
    def test_complete_exclusive_repeats_and_bytes(self):
        r=tool.report(self.root)
        self.assertTrue(r['identical_repetitions'])
        self.assertEqual(len(r['fixed']),8);self.assertEqual(len(r['elastic']),40)
        self.assertEqual(r['elastic'][0]['receipt']['copy_requests'],'64')
        self.assertEqual(r['elastic'][0]['provider_exclusive'],[40,20,10])
    def test_missing_region(self):
        (self.root/'SMALL/elastic2/counts.0').unlink()
        with self.assertRaisesRegex(ValueError,'incomplete'):tool.report(self.root)
    def test_duplicate_region(self):
        p=self.root/'SMALL/base-aa1';(p/'counts.extra').write_bytes((p/'counts.0').read_bytes())
        with self.assertRaisesRegex(ValueError,'duplicate'):tool.report(self.root)
    def test_same_totals_different_function_fails(self):
        p=self.root/'LARGE/elastic2/counts.0';r=json.loads(p.read_text());r['functions']['host'][0]+=1;r['functions']['allocation_acquire'][0]-=1;p.write_text(json.dumps(r))
        with self.assertRaisesRegex(ValueError,'nonidentical'):tool.report(self.root)
    def test_telemetry_changed_operation_fails(self):
        p=self.root/'SMALL/bytes/receipts.csv';p.write_text(p.read_text().replace(',600,600,',',601,600,'))
        with self.assertRaisesRegex(ValueError,'telemetry changed'):tool.report(self.root)
    def test_instrumented_counts_fails(self):
        for label in ('elastic1','elastic2'):
            p=self.root/'SMALL'/label/'receipts.csv';p.write_text(p.read_text().replace(',0,0,0,',',64,0,0,'))
        with self.assertRaisesRegex(ValueError,'instrumented binary'):tool.report(self.root)
    def test_missing_receipt_fails(self):
        p=self.root/'LARGE/head-ab2/receipts.csv';p.write_text('\n'.join(p.read_text().splitlines()[:-1])+'\n')
        with self.assertRaisesRegex(ValueError,'invalid receipts'):tool.report(self.root)
if __name__=='__main__':unittest.main()
