# SPDX-License-Identifier: MPL-2.0
"""Reject incomplete or inconsistent evidence before drawing layout conclusions."""
import csv
from pathlib import Path
import tempfile
import unittest
from diagnose_session_layout import CASES, layout, measurement, counts, prefix


class SessionLayoutEvidenceTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)
        self.case = CASES[0]
        self.name = prefix(self.case, 'A', 'aa1')
        self.path = self.root / f'{self.name}.layout.csv'
        self.data = {}
        for i, name in enumerate(('session', 'inputs', 'program', 'symbols', 'result')):
            self.data.update({f'sizeof.{name}': 512, f'alignof.{name}': 8, f'address.{name}': 4096 * (i + 1)})
        members = {'symbols': ('entries', 'storage', 'index'), 'inputs': ('fact_pool',),
                   'result': ('edb_facts', 'idb_facts'), 'program': ('rules', 'facts')}
        for name, fields in members.items():
            for i, field in enumerate(fields):
                self.data.update({f'offsetof.{name}.{field}': i * 64,
                                  f'sizeof.{name}.{field}': 64,
                                  f'address.{name}.{field}': self.data[f'address.{name}'] + i * 64})
        self.data['address.resolved_program'] = self.data['address.program']
        self.data['address.resolved_symbols'] = self.data['address.symbols']
        self.data.update({k.replace('address.', 'address_mod64.', 1): v % 64
                          for k, v in list(self.data.items()) if k.startswith('address.')})
        self.write_layout()

    def write_layout(self):
        with self.path.open('w', newline='') as out:
            writer = csv.writer(out)
            writer.writerow(('key', 'value'))
            writer.writerows(self.data.items())

    def test_real_member_address_required_not_just_modulo(self):
        self.assertEqual(layout(self.path), self.data)
        self.data['address.symbols.entries'] += 64
        self.write_layout()
        with self.assertRaisesRegex(ValueError, 'member layout'):
            layout(self.path)

    def test_probe_must_match_live_dictionary(self):
        self.data['address.resolved_symbols'] += 64
        self.write_layout()
        with self.assertRaisesRegex(ValueError, 'live solver'):
            layout(self.path)

    def test_missing_hot_payload_rejected(self):
        del self.data['offsetof.result.edb_facts']
        self.write_layout()
        with self.assertRaisesRegex(ValueError, 'missing hot member'):
            layout(self.path)

    def test_member_outside_object_rejected(self):
        self.data['sizeof.symbols'] = 64
        self.write_layout()
        with self.assertRaisesRegex(ValueError, 'member layout'):
            layout(self.path)

    def test_duplicate_key_rejected(self):
        with self.path.open('a') as out:
            out.write('sizeof.session,512\n')
        with self.assertRaisesRegex(ValueError, 'duplicate'):
            layout(self.path)

    def test_summary_and_samples_must_agree(self):
        keys = ('profile', 'policy', 'order', 'values', 'size')
        row = dict(zip(keys, self.case))
        row.update(commit='revision', samples=301, min_us=1, median_us=1, p95_us=1)
        with (self.root / f'{self.name}.csv').open('w', newline='') as out:
            writer = csv.DictWriter(out, fieldnames=row)
            writer.writeheader(); writer.writerow(row)
        with (self.root / f'{self.name}.samples.csv').open('w', newline='') as out:
            writer = csv.DictWriter(out, fieldnames=(*keys[1:], 'sample', 'elapsed_us'))
            writer.writeheader()
            for i in range(301):
                writer.writerow(dict(zip(keys[1:], self.case[1:]), sample=i, elapsed_us=1))
        measurement(self.root, self.case, 'A', 'aa1', 'revision')
        with self.assertRaisesRegex(ValueError, 'revision'):
            measurement(self.root, self.case, 'A', 'aa1', 'other-revision')
        path = self.root / f'{self.name}.samples.csv'
        path.write_text(path.read_text().replace(',1\n', ',2\n'))
        with self.assertRaisesRegex(ValueError, 'summary/raw mismatch'):
            measurement(self.root, self.case, 'A', 'aa1', 'revision')
        path.write_text('\n'.join(path.read_text().splitlines()[:-1]) + '\n')
        with self.assertRaisesRegex(ValueError, 'inventory'):
            measurement(self.root, self.case, 'A', 'aa1', 'revision')

    def test_per_function_costs_must_cover_all_events(self):
        (self.root / 'probe.out').write_text('events: Ir Dr Dw\nsummary: 100 20 10\ntotals: 100 20 10\n')
        functions = self.root / 'probe.functions.txt'
        functions.write_text('80 15 8 source.c:solve\n20 5 2 source.c:helper\n')
        self.assertEqual(counts(self.root, 'probe')[0], (100, 20, 10))
        functions.write_text('80 15 8 source.c:solve\n')
        with self.assertRaisesRegex(ValueError, 'incomplete per-function'):
            counts(self.root, 'probe')


if __name__ == '__main__':
    unittest.main()
