# SPDX-License-Identifier: MPL-2.0
from pathlib import Path
import sys
import tempfile
import unittest
from unittest.mock import patch

BENCH = Path(__file__).resolve().parents[2] / 'bench'
sys.path.insert(0, str(BENCH))
import python_release_instructions as subject


class Function:
    def __init__(self, callback):
        self.callback = callback
    def __call__(self, *args):
        return self.callback(*args)


class ReleaseInstructionTests(unittest.TestCase):
    def test_scopes_count_cold_then_warm_and_never_separate_phases(self):
        scopes = []
        calls = []
        class Library:
            tail_begin = Function(lambda: scopes.append(['begin', len(calls)]))
            tail_end = Function(lambda name: scopes.append([name.decode(), len(calls)]))
        def measured(case, samples, warmup, telemetry):
            self.assertEqual((samples, warmup, telemetry), (501, 50, True))
            def request(phases=False):
                calls.append(phases)
                return [True], None
            wrapped = subject.workload.repeat_transaction(request, 1)
            for _ in range(552):
                self.assertEqual(wrapped(), ([True], None))
            for _ in range(501):
                wrapped(True)
            return {'output_sha256': 'checked'}
        with patch.object(subject.ctypes, 'CDLL', return_value=Library()), \
                patch.object(subject.workload, 'measure', side_effect=measured), \
                patch.object(subject.sys, 'getdlopenflags', return_value=2, create=True), \
                patch.object(subject.os, 'RTLD_NOW', 2, create=True), \
                patch.object(subject.os, 'RTLD_LAZY', 1, create=True):
            result = subject.worker('7-integer-prepared', Path('helper.so'))
        self.assertEqual(result['scopes'], list(subject.LABELS))
        expected = [0] + [51 + i for i in subject.INDICES]
        self.assertEqual([x[1] for x in scopes if x[0] == 'begin'], expected)
        self.assertEqual([x[1] for x in scopes if x[0] != 'begin'], [i + 1 for i in expected])
        self.assertEqual(len(calls), 1053)

    def test_parse_event_names_and_reject_missing_counts(self):
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / 'profile'
            path.write_text('desc: Trigger: Client Request: cold\nevents: Dr Ir Dw\nsummary: 2 10 3\n')
            self.assertEqual(subject.parse_profile(path),
                             {'scope': 'cold', 'counts': {'Ir': 10, 'Dr': 2, 'Dw': 3}})
            path.write_text('desc: Trigger: Client Request: total_436\nevents: Ir Dr Dw I1mr D1mr D1mw ILmr DLmr DLmw\nsummary: 577847 193430 102094 16151 6936 1094\n')
            self.assertEqual(subject.parse_profile(path)['counts'],
                             {'Ir': 577847, 'Dr': 193430, 'Dw': 102094})
            path.write_text('desc: Trigger: Client Request: total_422\nevents: Ir Dr Dw\nsummary: 10 2\n')
            with self.assertRaises(ValueError):
                subject.parse_profile(path)
            path.write_text('desc: Trigger: Program termination\nevents: Ir Dr Dw\nsummary: 10 2 3\n')
            self.assertIsNone(subject.parse_profile(path))

    def test_reject_other_report_before_consuming_binaries(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            (root / 'report.json').write_text('{}')
            with self.assertRaisesRegex(ValueError, 'wrong original report'):
                subject.original_report(root)


if __name__ == '__main__':
    unittest.main()
