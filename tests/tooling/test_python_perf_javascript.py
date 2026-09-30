# SPDX-License-Identifier: MPL-2.0
from collections import Counter
from pathlib import Path
import json
import sys
import tempfile
import unittest
from unittest.mock import patch

sys.path.insert(0, str(Path(__file__).resolve().parents[2] / 'bench'))
import python_javascript_diagnostic as subject


class Function:
    def __init__(self, callback):
        self.callback = callback
    def __call__(self, *args):
        return self.callback(*args)


class JavaScriptDiagnosticTests(unittest.TestCase):
    def test_two_calibration_pairs_precede_balanced_rounds(self):
        plan = subject.schedule()
        self.assertEqual(len(plan), 48)
        self.assertTrue(all(p.startswith('aa') for _, p in plan[:16]))
        self.assertTrue(all(p.startswith('ab') for _, p in plan[16:]))
        for role in subject.ROLES:
            self.assertEqual(Counter(order.index(role) for order in subject.ORDERS),
                             Counter({0: 2, 1: 2, 2: 2, 3: 2}))

    def test_both_identical_labels_keep_alerts_and_unperturbed_controls(self):
        samples = {role: {pas: {'case/total': [100000] * 31}
                         for r, pas in subject.schedule() if r == role}
                   for role in subject.ROLES}
        samples['base_copy']['ab2']['case/total'] = [130000] * 31
        samples['head_copy']['ab5']['case/total'] = [150000] * 31
        rows = subject.comparisons(samples)
        self.assertEqual(len(rows), 8)
        for base, head, index in [('base', 'base_copy', 2), ('head', 'head_copy', 5)]:
            row = next(r for r in rows if (r['base'], r['head'], r['metric']) == (base, head, 'median'))
            self.assertEqual(row['classification'],
                             ['slower' if i == index else 'indeterminate' for i in range(8)])
        row = next(r for r in rows if r['head'] == 'head' and r['metric'] == 'median')
        self.assertEqual(row['classification'], ['indeterminate'] * 8)

    def test_scopes_exclude_preparation_warmup_and_phase_loops(self):
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
            result = subject.worker('93-integer-solve', Path('helper.so'))
        self.assertEqual([n for label, n in scopes if label == 'begin'], [51+i for i in subject.INDICES])
        self.assertEqual([n for label, n in scopes if label != 'begin'], [52+i for i in subject.INDICES])
        self.assertEqual(result['scopes'], [f'total_{i:03d}' for i in subject.INDICES])
        self.assertEqual(len(calls), 1053)
        self.assertNotIn('samples', result)

    def test_original_digest_versions_harness_and_every_binary_are_required(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            report = dict(schema=4, comparison_only=False, release_eligible=True,
                          positive_control={'detected': True}, commits=subject.COMMITS,
                          environment=dict(run_id=subject.SOURCE_RUN, python=sys.version, packages={}),
                          harness={f: subject.perf.sha256(subject.perf.ROOT / f)
                                   for f in subject.perf.HARNESS_FILES}, binaries={})
            for commit in subject.COMMITS.values():
                for config in {config for config, _ in subject.FIXTURES}:
                    folder = root / commit / config
                    folder.mkdir(parents=True)
                    (folder / 'sdk.bin').write_bytes(b'original')
                    report['binaries'][f'{commit}/{config}'] = {'sdk.bin': subject.perf.sha256(folder / 'sdk.bin')}
            path = root / 'report.json'
            path.write_text(json.dumps(report))
            with self.assertRaisesRegex(ValueError, 'wrong original report digest'):
                subject.originals(root)
            with patch.object(subject, 'SOURCE_SHA256', subject.perf.sha256(path)):
                _, packages = subject.originals(root)
                for config, _ in subject.FIXTURES:
                    for role in ('base', 'head'):
                        self.assertEqual(packages[role, config], packages[role+'_copy', config])
                binary = root / subject.COMMITS['head'] / 'LARGE-default/sdk.bin'
                binary.write_bytes(b'changed')
                with self.assertRaisesRegex(ValueError, 'changed original binary'):
                    subject.originals(root)
                binary.write_bytes(b'original')
            for field in ('python', 'harness', 'commit', 'positive'):
                bad = json.loads(json.dumps(report))
                if field == 'python':
                    bad['environment']['python'] = '0.0.0'
                elif field == 'harness':
                    bad['harness'][subject.perf.HARNESS_FILES[0]] = 'changed'
                elif field == 'commit':
                    bad['commits']['head'] = '0' * 40
                else:
                    bad['positive_control']['detected'] = False
                path.write_text(json.dumps(bad))
                with patch.object(subject, 'SOURCE_SHA256', subject.perf.sha256(path)), self.assertRaises(ValueError):
                    subject.originals(root)


if __name__ == '__main__':
    unittest.main()
