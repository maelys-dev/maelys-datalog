# SPDX-License-Identifier: MPL-2.0
"""Same-transaction telemetry and fail-closed artifact validation."""
from copy import deepcopy
import gc
from pathlib import Path
import sys
import time
from types import SimpleNamespace
import unittest
from unittest.mock import patch

sys.path.insert(0, str(Path(__file__).resolve().parents[2] / 'bench'))
import python_telemetry as telemetry


def synthetic(samples, duration=20000, warmup=50):
    """Explicit, deterministic fixture for the full driver, not perf evidence."""
    requests, calibration = [], []
    clock = 1_000_000
    def row(duration):
        nonlocal clock
        start = clock
        clock += duration + 100
        return dict(start_ns=start, end_ns=start + duration, thread_start_ns=start,
                    thread_end_ns=start + duration, cpu_before=0, cpu_after=0,
                    voluntary=0, involuntary=0, minor_faults=0, major_faults=0)
    def calibrate(loop, i):
        calibration.append(dict(row(20000), loop=loop, next_sample=i, iterations=64))
    for loop, count in [('cold', 1), ('warmup', warmup if samples else 0),
                        ('total', samples), ('phases', samples)]:
        for i in range(count):
            if loop in ('total', 'phases') and i % 32 == 0:
                calibrate(loop, i)
            requests.append(dict(row(duration), loop=loop, sample=i))
        if loop in ('total', 'phases') and samples:
            calibrate(loop, samples)
    return dict(version=1, requests=requests, calibration=calibration,
                calibration_every=32, calibration_budget_ns=20000,
                gc_overflow=False, cpu_source='test', collections=[])


class TelemetryTests(unittest.TestCase):
    def test_same_interval_and_endpoint_ids_are_recorded(self):
        before = SimpleNamespace(ru_nvcsw=3, ru_nivcsw=4, ru_minflt=20, ru_majflt=0)
        after = SimpleNamespace(ru_nvcsw=4, ru_nivcsw=6, ru_minflt=23, ru_majflt=0)
        recorder = telemetry.Recorder(0, 0)
        recorder.getcpu = iter((2, 3)).__next__
        with patch.object(telemetry.resource, 'getrusage', side_effect=[before, after]), \
                patch.object(telemetry.time, 'thread_time_ns', side_effect=[10, 112]), \
                patch.object(telemetry.time, 'perf_counter_ns', side_effect=[100, 200]):
            self.assertEqual(recorder.measure(lambda phases: ([True], None), False), (100, [True], None))
        result = recorder.export()['requests'][0]
        self.assertEqual(result, dict(start_ns=100, end_ns=200, thread_start_ns=10,
                                     thread_end_ns=112, cpu_before=2, cpu_after=3,
                                     voluntary=1, involuntary=2, minor_faults=3,
                                     major_faults=0, loop='cold', sample=0))
        with self.assertRaises(ValueError):
            recorder.measure(lambda phases: ([], None), False)

    def test_unavailable_cpu_is_explicit(self):
        with patch.object(telemetry.os, 'sched_getcpu', None, create=True):
            # Exercise non-Linux fallback without inventing a CPU id.
            with patch.object(telemetry, 'hasattr', return_value=False, create=True), \
                    patch.object(telemetry.platform, 'system', return_value='Darwin'):
                getcpu, source = telemetry.cpu_reader()
        self.assertEqual((getcpu(), source), (-1, 'unavailable'))

    def test_linux_libc_cpu_reader_on_python_without_os_wrapper(self):
        function = unittest.mock.Mock(return_value=7)
        with patch.object(telemetry, 'hasattr', return_value=False, create=True), \
                patch.object(telemetry.platform, 'system', return_value='Linux'), \
                patch.object(telemetry.ctypes, 'CDLL', return_value=SimpleNamespace(sched_getcpu=function)):
            reader, source = telemetry.cpu_reader()
        self.assertEqual((reader(), source), (7, 'libc.sched_getcpu'))
        self.assertEqual(function.argtypes, [])
        self.assertIs(function.restype, telemetry.ctypes.c_int)

    def test_gc_callback_overflow_fails_outside_callback_and_state_restores(self):
        callbacks = list(gc.callbacks)
        recorder = telemetry.Recorder(0, 0)
        with recorder:
            recorder.measure(lambda phases: (gc.collect(0), None), False)
        event = recorder.export()['collections'][0]
        request = recorder.export()['requests'][0]
        self.assertLessEqual(request['start_ns'], event['start_ns'])
        self.assertLessEqual(event['start_ns'], event['end_ns'])
        self.assertLessEqual(event['end_ns'], request['end_ns'])
        self.assertEqual(event['generation'], 0)
        recorder.gc_count = recorder.gc_capacity
        recorder.gc_event('start', dict(generation=0))
        recorder.gc_event('stop', dict(generation=0, collected=0, uncollectable=0))
        with self.assertRaises(ValueError):
            recorder.export()
        with self.assertRaises(RuntimeError), recorder:
            raise RuntimeError('transaction failed')
        self.assertEqual(gc.callbacks, callbacks)

    def test_wait_cpu_and_forced_gc_controls_use_real_recorder(self):
        def wait(phases):
            time.sleep(.004)
            return [], None
        recorder = telemetry.Recorder(0, 0)
        wall, _, _ = recorder.measure(wait, False)
        row = recorder.export()['requests'][0]
        self.assertGreater(wall - (row['thread_end_ns'] - row['thread_start_ns']), 2_000_000)
        def busy(phases):
            stop = time.thread_time_ns() + 4_000_000
            while time.thread_time_ns() < stop:
                pass
            return [], None
        recorder = telemetry.Recorder(0, 0)
        recorder.measure(busy, False)
        row = recorder.export()['requests'][0]
        self.assertGreaterEqual(row['thread_end_ns'] - row['thread_start_ns'], 4_000_000)

    def test_calibration_preserves_zero_work_when_descheduled(self):
        recorder = telemetry.Recorder(1, 0)
        with patch.object(telemetry.time, 'perf_counter_ns', side_effect=[100, 25100, 26100]):
            recorder.calibrate('total', 0)
        values = dict(zip(telemetry.CALIBRATION_FIELDS, recorder.calibration_values))
        self.assertEqual(values['iterations'], 0)
        self.assertEqual(values['end_ns'] - values['start_ns'], 26000)

    def test_validation_preserves_every_sample_and_rejects_incomplete_or_unpaired_data(self):
        for count in (0, 1, 31, 32, 501):
            value = dict(cold=20000, samples={p: [20000] * count
                         for p in ('input', 'solve', 'query', 'close', 'total')},
                         telemetry=synthetic(count), sample_storage='fixed')
            original = deepcopy(value)
            telemetry.validate(value, count, 50)
            self.assertEqual(value, original)
        for mutation in ('missing', 'phase_pair', 'duration', 'calibration', 'overlap', 'gc_overflow', 'storage'):
            value = dict(cold=20000, samples={p: [20000] * 501
                         for p in ('input', 'solve', 'query', 'close', 'total')},
                         telemetry=synthetic(501), sample_storage='fixed')
            data = value['telemetry']
            if mutation == 'missing': data['requests'].pop()
            elif mutation == 'phase_pair': data['requests'][51]['loop'] = 'phases'
            elif mutation == 'duration': value['samples']['total'][316] += 100
            elif mutation == 'calibration': data['calibration'].pop()
            elif mutation == 'overlap': data['calibration'][0]['end_ns'] += 1_000_000
            elif mutation == 'gc_overflow': data['gc_overflow'] = True
            else: value['sample_storage'] = 'growing'
            with self.subTest(mutation=mutation), self.assertRaises(ValueError):
                telemetry.validate(value, 501, 50)


if __name__ == '__main__':
    unittest.main()
