# SPDX-License-Identifier: MPL-2.0
"""Offline null cross-screening contracts, not performance measurements."""
from contextlib import redirect_stdout
from copy import deepcopy
import hashlib
from io import StringIO
import json
from pathlib import Path
import sys
import tempfile
import unittest
from unittest.mock import patch

ROOT = Path(__file__).resolve().parents[2]
sys.path[:0] = [str(ROOT / 'bench'), str(ROOT / 'tools')]
import python_perf as perf
import report_python_null_cross as offline


def row(ref, phase, metric, delta, floor=.02):
    classes = ['slower' if d > floor else 'faster' if d < -floor else 'indeterminate' for d in delta]
    return dict(reference=ref, scenario=f'SMALL-Release/7-symbol-solve/{phase}',
                metric=metric, aa_floor=floor, delta=delta, classification=classes,
                review_required='slower' in classes)


def fixture():
    nulls, candidates = [], []
    for ref in ('base', 'anchor'):
        base = ref == 'base'
        nulls += [row(ref, 'total', 'median', [.10, 0] if base else [.03, -.04]),
                  row(ref, 'total', 'p95', [.04, .04] if base else [.10, .12]),
                  row(ref, 'input', 'min', [.10, .20] if base else [0, 0], .20 if base else .01)]
        candidates += [row(ref, 'total', 'median', [.20, .20] if base else [.08, .08]),
                       row(ref, 'total', 'p95', [.05, .01] if base else [.01, .13]),
                       row(ref, 'input', 'min', [.30, 0] if base else [0, 0])]
        # Legitimately different statistic choices across the two references.
        for metric in (('min',) if base else ('median', 'p95')):
            nulls.append(row(ref, 'query', metric, [.40, .50]))
            candidates.append(row(ref, 'query', metric, [.80, .80]))
        for metric in ('median', 'p95'):
            nulls.append(row(ref, 'cold', metric, [.90, .90] if base else [0, 0]))
            candidates.append(row(ref, 'cold', metric, [10, 10]))
    commits = dict(base='release', anchor='anchor', head='candidate', historical='history')
    variants = {role: dict(commit=c, request_repetitions=n, sampling_identity=i)
                for role, (c, n, i) in perf.measurement_variants(commits).items()}
    return dict(schema=4, commits=commits, variants=variants, status='review_required',
                environment=dict(run_id='synthetic'), positive_control=dict(detected=True),
                rows=perf.screen_with_null(candidates, nulls), null_control=dict(rows=nulls))


class NullCrossTests(unittest.TestCase):
    def test_directions_rounds_and_candidate_denominators_are_preserved(self):
        report = fixture()
        original = deepcopy(report)
        result = perf.null_cross_check(report)
        self.assertEqual(report, original)
        self.assertTrue(result['informative_only'])
        self.assertFalse(result['changes_review_status'])
        base, anchor = result['directions']
        self.assertEqual((base['reference'], base['envelope_reference']), ('base', 'anchor'))
        self.assertEqual((anchor['reference'], anchor['envelope_reference']), ('anchor', 'base'))
        a, b = base['summary']['all_warm'], anchor['summary']['all_warm']
        self.assertEqual((a['rows'], b['rows']), (3, 3))
        self.assertEqual(a['null'], dict(rounds=[1, 0], rates=[1/3, 0], any_round=1, single_round=1, both_rounds=0))
        self.assertEqual(a['envelope_exceeded']['rounds'], [2, 1])
        self.assertEqual(b['null'], dict(rounds=[1, 1], rates=[1/3, 1/3], any_round=1, single_round=0, both_rounds=1))
        self.assertEqual(a['candidate']['rounds'], [3, 1])
        self.assertEqual(b['candidate']['rounds'], [1, 2])
        self.assertEqual(base['summary']['total']['rows'], 2)
        self.assertEqual(base['summary']['total']['candidate']['rounds'], [2, 1])
        self.assertEqual(anchor['summary']['metric:p95']['null']['both_rounds'], 1)
        self.assertEqual(base['summary']['config:SMALL-Release'], a)
        self.assertEqual(base['summary']['phase:input']['null']['rounds'], [0, 0])
        # A larger descriptive null count must never clear original candidate alerts.
        self.assertEqual(report['status'], 'review_required')
        self.assertEqual(sum(r['review_required'] for r in report['rows']), 8)

    def test_own_floor_absolute_other_gaps_and_strict_boundaries(self):
        result = perf.null_cross_check(fixture())
        base, anchor = result['directions']
        input_row = next(r for r in base['rows'] if r['scenario'].endswith('/input'))
        self.assertEqual(input_row['envelope_exceeded'], [True, True])
        self.assertEqual(input_row['null_alerts'], [False, False])
        median = next(r for r in base['rows'] if r['metric'] == 'median')
        self.assertEqual(median['other_null_band'], .04)  # negative other-round gap counts
        anchor_input = next(r for r in anchor['rows'] if r['scenario'].endswith('/input'))
        self.assertEqual(anchor_input['other_null_band'], .20)
        report = fixture()
        for r in report['null_control']['rows']:
            if r['reference'] == 'base' and r['metric'] == 'median' and r['scenario'].endswith('/total'):
                r.update(delta=[.04, -.10], classification=['slower', 'faster'])
        report['rows'] = perf.screen_with_null(
            [dict(r, review_required=r['aa_review_required']) for r in report['rows']], report['null_control']['rows'])
        median = next(r for r in perf.null_cross_check(report)['directions'][0]['rows'] if r['metric'] == 'median')
        self.assertEqual(median['null_alerts'], [False, False])  # equality and faster are not alarms

    def test_mismatched_statistics_and_cold_are_excluded_not_silently_pooled(self):
        result = perf.null_cross_check(fixture())
        base, anchor = result['directions']
        self.assertEqual((base['total_warm_rows'], anchor['total_warm_rows']), (4, 5))
        self.assertEqual([r['metric'] for r in base['unmatched']], ['min'])
        self.assertEqual([r['metric'] for r in anchor['unmatched']], ['median', 'p95'])
        self.assertEqual((base['excluded_cold_rows'], anchor['excluded_cold_rows']), (2, 2))
        self.assertTrue(all(not r['scenario'].endswith(('/cold', '/query'))
                            for direction in result['directions'] for r in direction['rows']))

    def test_shared_samples_are_unavailable_not_a_zero_rate(self):
        report = fixture()
        report['commits']['base'] = report['commits']['anchor']
        for role in ('base', 'base_null'):
            report['variants'][role] = deepcopy(report['variants'][role.replace('base', 'anchor')])
        result = perf.null_cross_check(report)
        self.assertFalse(result['available'])
        self.assertEqual(result['reason'], 'shared_null_samples')
        self.assertEqual(result['directions'], [])
        self.assertIn('not a zero alert rate', '\n'.join(perf.null_cross_markdown(result)))

    def test_no_common_statistics_is_explicitly_unavailable(self):
        report = fixture()
        for rows in (report['rows'], report['null_control']['rows']):
            rows[:] = [r for r in rows if r['scenario'].endswith('/query')]
        result = perf.null_cross_check(report)
        self.assertFalse(result['available'])
        self.assertEqual(result['reason'], 'no_matching_warm_statistics')

    def test_incomplete_or_inconsistent_evidence_is_rejected(self):
        mutations = {
            'schema': lambda r: r.update(schema=3),
            'missing scenario': lambda r: r['null_control']['rows'].__setitem__(slice(None), r['null_control']['rows'][2:]),
            'missing statistic': lambda r: r['null_control']['rows'].pop(0),
            'duplicate': lambda r: r['null_control']['rows'].append(r['null_control']['rows'][0]),
            'nan': lambda r: r['null_control']['rows'][0].update(aa_floor=float('nan')),
            'negative floor': lambda r: r['null_control']['rows'][0].update(aa_floor=-.1),
            'missing round': lambda r: r['null_control']['rows'][0].update(delta=[.10]),
            'wrong raw classification': lambda r: r['null_control']['rows'][0].update(classification=['slower'] * 2),
            'wrong raw flag': lambda r: r['null_control']['rows'][0].update(review_required=False),
            'wrong candidate screening': lambda r: r['rows'][0].update(screening=['no_aa_slowdown'] * 2),
            'wrong candidate review': lambda r: r['rows'][0].update(review_required=False),
            'other binary': lambda r: r['variants']['base_null'].update(commit='different'),
            'injected null': lambda r: r['variants']['base_null'].update(request_repetitions=3),
            'ordinary samples reused': lambda r: r['variants']['base_null'].update(sampling_identity='reference'),
        }
        for name, mutation in mutations.items():
            report = fixture()
            mutation(report)
            with self.subTest(name=name), self.assertRaises(ValueError):
                perf.null_cross_check(report)

    def test_offline_supplement_never_measures_or_overwrites_history(self):
        with tempfile.TemporaryDirectory() as directory:
            source = Path(directory) / 'report.json'
            output = Path(directory) / 'supplement'
            source.write_text(json.dumps(fixture()) + '\n')
            original = source.read_bytes()
            with patch.object(perf, 'build', side_effect=AssertionError('no builds')), \
                    patch.object(perf, 'command', side_effect=AssertionError('no subprocesses')), \
                    patch.object(perf.subprocess, 'run', side_effect=AssertionError('no measurements')), \
                    redirect_stdout(StringIO()):
                offline.run(source, output)
                for existing in (output, source.parent):
                    with self.assertRaises(FileExistsError):
                        offline.run(source, existing)
            self.assertEqual(source.read_bytes(), original)
            supplement = json.loads((output / 'null-cross.json').read_text())
            self.assertEqual(supplement['source_report_sha256'], hashlib.sha256(original).hexdigest())
            self.assertEqual(supplement['source_status'], 'review_required')
            self.assertFalse(supplement['release_eligible'])
            self.assertFalse(supplement['measurement_performed'])
            self.assertTrue(supplement['positive_control_detected'])
            text = (output / 'null-cross.md').read_text()
            self.assertIn('1/3 (33.33%)', text)
            self.assertIn('round 1 **2/3 (66.67%)**, round 2 **1/3 (33.33%)**', text)
            self.assertIn('not independent statistical trials', text)
            self.assertIn('unmatched statistics', text)


if __name__ == '__main__':
    unittest.main()
