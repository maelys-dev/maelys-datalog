# SPDX-License-Identifier: MPL-2.0
"""Diagnostic plumbing only; not performance measurements."""
from pathlib import Path
import gc
import sys
import unittest
from unittest.mock import patch
from types import SimpleNamespace
sys.path.insert(0, str(Path(__file__).resolve().parents[2] / 'bench'))
import python_tail_workload as probe
import python_tail_diagnostic as driver


class TailTests(unittest.TestCase):
    def test_fixed_balanced_schedule_preserves_every_pass(self):
        plan = driver.schedule()
        self.assertEqual(plan[:8], [(r, f'aa{i}') for r in ('base', 'head') for i in range(4)])
        self.assertEqual(len(plan), 32)
        for i in range(12):
            self.assertEqual(plan[8+2*i:10+2*i], [(r, f'ab{i}') for r in
                             (('base', 'head') if i % 2 == 0 else ('head', 'base'))])

    def test_observer_preserves_answer_and_records_cpu_and_os_deltas(self):
        before = SimpleNamespace(ru_nvcsw=3, ru_nivcsw=4, ru_minflt=20, ru_majflt=0)
        after = SimpleNamespace(ru_nvcsw=4, ru_nivcsw=6, ru_minflt=23, ru_majflt=0)
        observer = probe.Observer(1)
        answer = ([True, False], None)
        with patch.object(probe.resource, 'getrusage', side_effect=[before, after]), \
                patch.object(probe.time, 'thread_time_ns', side_effect=[5, 85]), \
                patch.object(probe.time, 'perf_counter_ns', side_effect=[100, 200]):
            self.assertIs(observer.wrap(lambda phases: answer, 1)(False), answer)
        self.assertEqual(observer.records(), [dict(start_ns=100, wall_ns=100, thread_ns=80,
                         voluntary=1, involuntary=2, minor_faults=3, major_faults=0)])
        with self.assertRaises(ValueError):
            observer.wrap(lambda phases: answer, 1)()
        with self.assertRaises(ValueError):
            observer.wrap(lambda phases: answer, 3)

    def test_gc_overlap_and_negative_clock_gap_are_preserved(self):
        row = dict(start_ns=100, wall_ns=300000, thread_ns=300001,
                   voluntary=0, involuntary=0, minor_faults=0, major_faults=0)
        value = dict(mode='observe', samples={'total': [350000]}, total_range=[0,1],
                     observed=[row], collections=[dict(start_ns=50, end_ns=150)])
        summary = driver.describe(value)
        self.assertEqual(summary['events']['gc_overlap_ns'], 50)
        self.assertEqual(summary['wall_minus_thread_ns']['median'], -1)
        self.assertEqual(summary['observed_above_200us'][0]['sample'], 0)
        self.assertEqual(len(value['samples']['total']), 1)

    def test_gc_callback_retains_generation_and_duration(self):
        observer = probe.Observer(1)
        with patch.object(probe.time, 'perf_counter_ns', side_effect=[30, 80]):
            observer.gc_event('start', dict(generation=1))
            observer.gc_event('stop', dict(generation=1, collected=7))
        self.assertEqual(observer.collections, [dict(start_ns=30, end_ns=80, generation=1, collected=7)])

    def test_plain_path_and_exception_restore_global_state(self):
        original = probe.workload.repeat_transaction
        callbacks = list(gc.callbacks)
        enabled = gc.isenabled()
        def plain(*args):
            self.assertIs(probe.workload.repeat_transaction, original)
            return {}
        with patch.object(probe.workload, 'measure', side_effect=plain):
            result = probe.measure('7-integer-prepared', 'plain')
            self.assertFalse(result['release_eligible'])
            self.assertNotIn('observed', result)
        def fail(*args):
            self.assertFalse(gc.isenabled())
            raise RuntimeError('query mismatch')
        with patch.object(probe.workload, 'measure', side_effect=fail), self.assertRaises(RuntimeError):
            probe.measure('7-integer-prepared', 'gc-off')
        self.assertIs(probe.workload.repeat_transaction, original)
        self.assertEqual(gc.callbacks, callbacks)
        self.assertEqual(gc.isenabled(), enabled)

    def test_missing_observations_fail_closed(self):
        with patch.object(probe.workload, 'measure', return_value={}), self.assertRaises(ValueError):
            probe.measure('7-integer-prepared', 'observe')


if __name__ == '__main__':
    unittest.main()
