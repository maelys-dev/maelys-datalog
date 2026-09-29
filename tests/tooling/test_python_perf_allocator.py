# SPDX-License-Identifier: MPL-2.0
from collections import Counter
import os
from pathlib import Path
import sys
import tempfile
import unittest
from unittest.mock import patch

sys.path.insert(0, str(Path(__file__).resolve().parents[2] / 'bench'))
import python_allocator_control as control


class AllocatorControlTests(unittest.TestCase):
    def test_balanced_schedule_and_two_aa_pairs_precede_comparisons(self):
        plan = control.schedule()
        self.assertEqual(len(plan), 32)
        self.assertTrue(all(p.startswith('aa') for _, p in plan[:16]))
        self.assertTrue(all(p.startswith('ab') for _, p in plan[16:]))
        for variant in control.VARIANTS:
            self.assertEqual(Counter(order.index(variant) for order in control.ORDERS),
                             Counter({0: 1, 1: 1, 2: 1, 3: 1}))
            self.assertEqual({p for v, p in plan if v == variant},
                             {f'{kind}{i}' for kind in ('aa', 'ab') for i in range(4)})

    def test_intervention_is_only_explicit_fixed_condition_and_no_inherited_tuning(self):
        plain = control.environment({'PATH': 'retained'}, Path('/same/package'), 'base_default')
        fixed = control.environment({'PATH': 'retained'}, Path('/same/package'), 'base_fixed')
        self.assertEqual(fixed, dict(plain, **control.TREATMENT))
        for key in ('MALLOC_TRIM_THRESHOLD_', 'MALLOC_TOP_PAD_', 'GLIBC_TUNABLES', 'LD_PRELOAD'):
            with self.subTest(key=key), self.assertRaisesRegex(ValueError, 'inherited'):
                control.environment({key: 'unexpected'}, Path('/p'), 'head_default')

    def test_wrong_artifact_rejected_before_execution(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory); (root / 'report.json').write_text('{}')
            with self.assertRaisesRegex(ValueError, 'wrong original report'):
                control.original_report(root)

    def test_raw_aa_and_each_round_are_retained(self):
        samples = {v: {p: [100000] * 31 for r, p in control.schedule() if r == v}
                   for v in control.VARIANTS}
        for i in range(4):
            samples['head_default'][f'ab{i}'] = [160000] * 31
        samples['head_fixed']['ab2'] = [105000] * 31
        rows = control.comparisons(samples)
        row = next(r for r in rows if r['base'] == 'base_default' and
                   r['head'] == 'head_default' and r['metric'] == 'median')
        self.assertEqual(row['classification'], ['slower'] * 4)
        row = next(r for r in rows if r['base'] == 'base_fixed' and
                   r['head'] == 'head_fixed' and r['metric'] == 'median')
        self.assertEqual(row['classification'], ['indeterminate', 'indeterminate', 'slower', 'indeterminate'])

    def test_trace_scopes_exclude_startup_and_never_confuse_growth_with_trim(self):
        text = ('brk(NULL) = 0x1000\n'
                'brk(0x9000) = 0x9000\n'
                'write(2, "ALLOC_SCOPE_BEGIN\\n", 18) = 18\n'
                'brk(0x7000) = 0x7000\n'
                'mmap(NULL, 8192, PROT_READ|PROT_WRITE, MAP_PRIVATE|MAP_ANONYMOUS, -1, 0) = 0x8000\n'
                'munmap(0x8000, 8192) = 0\n'
                'write(2, "ALLOC_SCOPE_END\\n", 16) = 16\n')
        result = control.parse_trace(text, 1)
        self.assertEqual(result['totals'], dict(brk_calls=1, brk_shrink_calls=1,
                                              brk_shrink_bytes=8192, mmap_calls=1, munmap_calls=1))
        for bad in (text.replace('ALLOC_SCOPE_END', 'missing'), text + text):
            with self.assertRaises(ValueError):
                control.parse_trace(bad, 1)

    def test_worker_marks_only_warm_total_requests_not_cold_warmup_or_phases(self):
        markers, calls = [], []
        def measured(case, samples, warmup, telemetry):
            self.assertEqual((case, samples, warmup, telemetry), (control.CASE, 3, 50, True))
            def transaction(phases=False):
                calls.append(phases); return [True], None
            request = control.workload.repeat_transaction(transaction, 1)
            for _ in range(54): request()
            for _ in range(3): request(True)
            return {'ok': True}
        with patch.object(control.workload, 'measure', side_effect=measured), \
                patch.object(os, 'write', side_effect=lambda fd, value: markers.append((len(calls), value))):
            self.assertEqual(control.trace_worker(3), {'ok': True})
        self.assertEqual([n for n, b in markers if b == b'ALLOC_SCOPE_BEGIN\n'], [51, 52, 53])
        self.assertEqual([n for n, b in markers if b == b'ALLOC_SCOPE_END\n'], [52, 53, 54])
        self.assertEqual(len(calls), 57)


if __name__ == '__main__':
    unittest.main()
