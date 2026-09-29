# SPDX-License-Identifier: MPL-2.0
from collections import Counter
from pathlib import Path
import sys
import tempfile
import unittest
from unittest.mock import patch

sys.path.insert(0, str(Path(__file__).resolve().parents[2] / 'bench'))
import python_prepared_diagnostic as subject


class Function:
    def __init__(self, callback):
        self.callback = callback
    def __call__(self, *args):
        return self.callback(*args)


class PreparedDiagnosticTests(unittest.TestCase):
    def test_counterbalances_all_labels_and_preserves_same_binary_alerts(self):
        for role in subject.ROLES:
            self.assertEqual(Counter(order.index(role) for order in subject.ORDERS),
                             Counter({0: 2, 1: 2, 2: 2, 3: 2}))
        self.assertEqual(len(subject.schedule()), 48)
        samples = {role: {pas: {'case/total': [100000] * 31} for r, pas in subject.schedule() if r == role}
                   for role in subject.ROLES}
        for i in range(8):
            samples['head_copy'][f'ab{i}']['case/total'] = [130000 if i == 2 else 100000] * 31
        rows = subject.comparisons(samples)
        copy = next(r for r in rows if r['head'] == 'head_copy' and r['metric'] == 'median')
        self.assertEqual(copy['classification'], ['indeterminate'] * 2 + ['slower'] + ['indeterminate'] * 5)

    def test_counts_exact_complete_requests_without_setup_or_phase_loops(self):
        scopes, calls = [], []
        class Library:
            tail_begin = Function(lambda: scopes.append(('begin', len(calls))))
            tail_end = Function(lambda label: scopes.append((label.decode(), len(calls))))
        def measure(case, samples, warmup, telemetry):
            self.assertEqual((samples, warmup, telemetry), (501, 50, True))
            def transaction(phases=False):
                calls.append(phases)
                return [True], None
            wrapped = subject.workload.repeat_transaction(transaction, 1)
            for _ in range(552):
                self.assertEqual(wrapped(), ([True], None))
            for _ in range(501):
                wrapped(True)
            return {'output_sha256': 'checked'}
        with patch.object(subject.ctypes, 'CDLL', return_value=Library()), \
                patch.object(subject.workload, 'measure', side_effect=measure):
            result = subject.worker('93-integer-prepared', Path('helper.so'))
        self.assertEqual([n for label, n in scopes if label == 'begin'], [51+i for i in subject.INDICES])
        self.assertEqual([n for label, n in scopes if label != 'begin'], [52+i for i in subject.INDICES])
        self.assertEqual(result['scopes'], [f'total_{i:03d}' for i in subject.INDICES])
        self.assertEqual(len(calls), 1053)
        self.assertNotIn('samples', result)  # Valgrind latency is never reported.

    def test_wrong_report_fails_before_binary_consumption(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            (root / 'report.json').write_text('{}')
            with self.assertRaisesRegex(ValueError, 'wrong original report'):
                subject.originals({'release': root, 'parent': root})

    def test_empty_final_dump_is_ignored_but_empty_requested_dump_fails(self):
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / 'callgrind.out'
            path.write_text('events: Ir Dr Dw\nsummary: 0\ntotals: 0\n')
            self.assertIsNone(subject.scoped_counts(path))
            path.write_text('desc: Trigger: Client Request: total_000\n' + path.read_text())
            with self.assertRaisesRegex(ValueError, 'missing counters'):
                subject.scoped_counts(path)


if __name__ == '__main__':
    unittest.main()
