# SPDX-License-Identifier: MPL-2.0
import copy
from pathlib import Path
import tempfile
import unittest
from unittest.mock import patch
import json

from bounded_residual import spec, validate_schedule, strict_counts, read_stats, report, fixtures, TARGET, PREPARED


class BoundedEvidenceTests(unittest.TestCase):
    def test_fixed_counterbalanced_schedule(self):
        p = spec()
        p['schedule'][-1], p['schedule'][-2] = p['schedule'][-2], p['schedule'][-1]
        with self.assertRaisesRegex(ValueError, 'reverse'):
            validate_schedule(p)

    def test_missing_aa_refused(self):
        p = spec()
        p['schedule'].pop(0)
        with self.assertRaises(ValueError):
            validate_schedule(p)

    def records(self):
        rows = []
        for role in 'ABC':
            for pad in (0, 16, 32, 48, 64, 128):
                for repeat in (1, 2):
                    rows.append(dict(driver='SMALL-session-controls', case=['derive', 'sorted', 'integer', '31'], role=role, pad=pad, repeat=repeat,
                                     total={'Ir': 110}, functions={TARGET: {'Ir': 100 if role != 'C' else 98, 'Dr': 20, 'Dw': 10},
                                     PREPARED: {'Ir': 10 if role != 'C' else 18, 'Dr': 2 if role != 'C' else 3, 'Dw': 1 if role != 'C' else 3}}))
        return rows

    def test_named_bounded_exception_only(self):
        rows = self.records()
        self.assertEqual(strict_counts(rows)['failures'], [])
        mutant = copy.deepcopy(rows)
        for r in mutant:
            if r['role'] == 'C':
                r['functions'][PREPARED]['Dr'] += 1
        self.assertTrue(any(f['kind'] == 'unexpected_increase' for f in strict_counts(mutant)['failures']))

    def test_placement_drift_is_not_hidden_by_totals(self):
        rows = self.records()
        rows[-1]['functions'][TARGET]['Dw'] += 1
        self.assertTrue(any(f['kind'] == 'repeat_or_placement_drift' for f in strict_counts(rows)['failures']))

    def test_raw_output_oracle_required(self):
        with tempfile.TemporaryDirectory() as directory:
            prefix = Path(directory) / 'solver'
            raw = Path(str(prefix) + '.samples.csv')
            raw.write_text('sample,elapsed_us,result\n' + ''.join(f'{i},12,17\n' for i in range(1000)))
            self.assertEqual(read_stats(prefix, True)['median_us'], 12)
            raw.write_text(raw.read_text().replace('999,12,17', '999,12,16'))
            with self.assertRaisesRegex(ValueError, 'oracle'):
                read_stats(prefix, True)

    def test_solver_statistics_match_historical_definitions(self):
        with tempfile.TemporaryDirectory() as directory:
            prefix = Path(directory) / 'solver'
            Path(str(prefix) + '.samples.csv').write_text('sample,elapsed_us,result\n' +
                ''.join(f'{i},{i+1},17\n' for i in range(1000)))
            values = read_stats(prefix, True)
            self.assertEqual(values['median_us'], 500.5)
            self.assertEqual(values['p95_us'], 950)

    def test_complete_report_keeps_both_noise_views_and_every_count_cell(self):
        p = spec()
        rows = []
        for driver in p['drivers']:
            for fixture in fixtures(p, driver):
                for r in self.records():
                    r['driver'], r['case'] = driver, fixture['case']
                    if 'solver' in driver:
                        r['functions'].pop(PREPARED)
                    rows.append(r)
        def synthetic_stats(prefix, solver=False):
            entry = p['schedule'][int(prefix.name.split('-')[0])]
            # Candidate AA is noisier; never hide the baseline-floor label.
            value = 20 if entry['role'] != 'C' else 22
            if entry['phase'] == 'AA' and entry['role'] == 'C' and entry['repeat'] % 2 == 0:
                value = 28
            return dict(min_us=value, median_us=value, p95_us=value, result_digest='17')
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            (root / 'counts').mkdir()
            (root / 'counts/counts.json').write_text(json.dumps(rows))
            with patch('bounded_residual.read_stats', synthetic_stats):
                report(root)
            verdict = json.loads((root / 'acceptance.json').read_text())
            self.assertEqual(len(verdict['cases']), 42)
            self.assertEqual(verdict['failures'], [])
            timings = json.loads((root / 'timing-comparisons.json').read_text())
            self.assertEqual(len(timings), 168)
            self.assertTrue(all(r['verdict'] == 'slower' and r['max_floor_verdict'] == 'indéterminé' for r in timings))
            rows.pop()
            (root / 'counts/counts.json').write_text(json.dumps(rows))
            with patch('bounded_residual.read_stats', synthetic_stats):
                with self.assertRaisesRegex(ValueError, 'inventory'):
                    report(root)


if __name__ == '__main__':
    unittest.main()
