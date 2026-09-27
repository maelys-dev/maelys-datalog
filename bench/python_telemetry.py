# SPDX-License-Identifier: MPL-2.0
"""Contemporaneous request observations, never a latency correction/filter."""
from array import array
import ctypes
import gc
import os
import platform
import resource
import time

VERSION = 1
CALIBRATION_EVERY = 32
CALIBRATION_NS = 20_000
FIELDS = ('start_ns', 'end_ns', 'thread_start_ns', 'thread_end_ns',
          'cpu_before', 'cpu_after', 'voluntary', 'involuntary',
          'minor_faults', 'major_faults')
GC_FIELDS = ('start_ns', 'end_ns', 'generation', 'collected', 'uncollectable')
CALIBRATION_FIELDS = FIELDS + ('loop_id', 'next_sample', 'iterations', 'checksum')
USAGE = getattr(resource, 'RUSAGE_THREAD', resource.RUSAGE_SELF)


def cpu_reader():
    if hasattr(os, 'sched_getcpu'):
        return os.sched_getcpu, 'os.sched_getcpu'
    # CPython 3.12 has no os.sched_getcpu. Use the documented Linux libc API.
    if platform.system() == 'Linux':
        try:
            function = ctypes.CDLL(None).sched_getcpu
            function.argtypes = []
            function.restype = ctypes.c_int
            if function() >= 0:
                return function, 'libc.sched_getcpu'
        except (AttributeError, OSError):
            pass
    return lambda: -1, 'unavailable'


class Recorder:
    def __init__(self, samples, warmup):
        if samples < 0 or warmup < 0:
            raise ValueError('negative sample/warmup count')
        self.samples = samples
        self.warmup = warmup if samples else 0
        self.capacity = 1 + self.warmup + 2 * samples
        self.count = 0
        self.values = array('q', [0]) * (self.capacity * len(FIELDS))
        self.gc_capacity = max(1024, self.capacity * 4)
        self.gc_values = array('q', [0]) * (self.gc_capacity * len(GC_FIELDS))
        self.gc_count = 0
        self.gc_start = None
        self.gc_overflow = False
        self.calibration_count = 0
        self.calibration_capacity = 2 * (len(range(0, samples, CALIBRATION_EVERY)) + 1) if samples else 0
        self.calibration_values = array('q', [0]) * (self.calibration_capacity * len(CALIBRATION_FIELDS))
        self.getcpu, self.cpu_source = cpu_reader()
        self.epoch_ns = time.time_ns()
        self.monotonic_ns = time.perf_counter_ns()

    def __enter__(self):
        gc.callbacks.append(self.gc_event)
        return self

    def __exit__(self, *exc):
        gc.callbacks.remove(self.gc_event)

    def gc_event(self, phase, info):
        if phase == 'start':
            self.gc_start = time.perf_counter_ns()
        elif self.gc_start is not None:
            end = time.perf_counter_ns()
            if self.gc_count == self.gc_capacity:
                # Exceptions in GC callbacks are ignored by Python. Fail outside.
                self.gc_overflow = True
            else:
                offset = self.gc_count * len(GC_FIELDS)
                self.gc_values[offset] = self.gc_start
                self.gc_values[offset + 1] = end
                self.gc_values[offset + 2] = info['generation']
                self.gc_values[offset + 3] = info['collected']
                self.gc_values[offset + 4] = info['uncollectable']
                self.gc_count += 1
            self.gc_start = None

    def begin(self):
        before = resource.getrusage(USAGE)
        cpu = self.getcpu()
        thread = time.thread_time_ns()
        return before, cpu, thread

    def finish(self, before, start, end):
        thread = time.thread_time_ns()
        cpu = self.getcpu()
        after = resource.getrusage(USAGE)
        usage, cpu_start, thread_start = before
        return (start, end, thread_start, thread, cpu_start, cpu,
                after.ru_nvcsw - usage.ru_nvcsw, after.ru_nivcsw - usage.ru_nivcsw,
                after.ru_minflt - usage.ru_minflt, after.ru_majflt - usage.ru_majflt)

    def measure(self, transaction, phases):
        if self.count == self.capacity:
            raise ValueError('request telemetry storage exhausted')
        before = self.begin()
        start = time.perf_counter_ns()
        answers, stamps = transaction(phases)
        end = time.perf_counter_ns()
        values = self.finish(before, start, end)
        offset = self.count * len(FIELDS)
        for i, value in enumerate(values):
            self.values[offset + i] = value
        self.count += 1
        return end - start, answers, stamps

    def calibrate(self, loop, next_sample):
        """Adjacent independent Python throughput; clocks/checks are included."""
        if self.calibration_count == self.calibration_capacity:
            raise ValueError('calibration telemetry storage exhausted')
        before = self.begin()
        start = time.perf_counter_ns()
        deadline = start + CALIBRATION_NS
        blocks = 0
        checksum = 0
        while time.perf_counter_ns() < deadline:
            for _ in range(32):
                checksum = (checksum + 1) & 65535
            blocks += 1
        end = time.perf_counter_ns()
        values = self.finish(before, start, end) + (('total', 'phases').index(loop), next_sample, 32 * blocks, checksum)
        offset = self.calibration_count * len(CALIBRATION_FIELDS)
        for i, value in enumerate(values):
            self.calibration_values[offset + i] = value
        self.calibration_count += 1

    def export(self):
        if (self.count != self.capacity or self.gc_overflow or self.gc_start is not None
                or self.calibration_count != self.calibration_capacity):
            raise ValueError('incomplete or overflowed request/GC telemetry')
        rows = [dict(zip(FIELDS, self.values[i * len(FIELDS):(i + 1) * len(FIELDS)]))
                for i in range(self.count)]
        groups = [('cold', 1), ('warmup', self.warmup),
                  ('total', self.samples), ('phases', self.samples)]
        offset = 0
        for loop, count in groups:
            for i in range(count):
                rows[offset + i].update(loop=loop, sample=i)
            offset += count
        collections = [dict(zip(GC_FIELDS, self.gc_values[i * len(GC_FIELDS):(i + 1) * len(GC_FIELDS)]))
                       for i in range(self.gc_count)]
        calibrations = [dict(zip(CALIBRATION_FIELDS, self.calibration_values[
            i * len(CALIBRATION_FIELDS):(i + 1) * len(CALIBRATION_FIELDS)]))
            for i in range(self.calibration_count)]
        for row in calibrations:
            row['loop'] = ('total', 'phases')[row.pop('loop_id')]
        return dict(version=VERSION, requests=rows, collections=collections,
                    calibration=calibrations, calibration_every=CALIBRATION_EVERY,
                    calibration_budget_ns=CALIBRATION_NS, gc_overflow=False,
                    gc_capacity=self.gc_capacity, gc_enabled=gc.isenabled(),
                    cpu_source=self.cpu_source, unavailable_cpu=-1,
                    usage_scope='thread' if hasattr(resource, 'RUSAGE_THREAD') else 'process',
                    epoch_anchor_ns=self.epoch_ns, monotonic_anchor_ns=self.monotonic_ns,
                    clocks={name: vars(time.get_clock_info(name))
                            for name in ('perf_counter', 'thread_time')})


def validate(value, samples, warmup):
    """Fail closed on incomplete telemetry; never classify or discard a sample."""
    data = value['telemetry']
    if data['version'] != VERSION or data['gc_overflow'] or value['sample_storage'] != 'fixed':
        raise ValueError('invalid telemetry protocol/storage')
    groups = [('cold', 1), ('warmup', warmup if samples else 0),
              ('total', samples), ('phases', samples)]
    expected = [(loop, i) for loop, count in groups for i in range(count)]
    rows = data['requests']
    if [(r['loop'], r['sample']) for r in rows] != expected:
        raise ValueError('incomplete/mislabelled request telemetry')
    previous = 0
    for row in rows:
        if not previous <= row['start_ns'] < row['end_ns'] or row['thread_end_ns'] < row['thread_start_ns']:
            raise ValueError('invalid telemetry clock interval')
        previous = row['end_ns']
        if any(row[name] < 0 for name in FIELDS[6:]):
            raise ValueError('negative resource counter delta')
        if data['cpu_source'] == 'unavailable' and (row['cpu_before'], row['cpu_after']) != (-1, -1):
            raise ValueError('invented unavailable CPU id')
        if row['loop'] in ('cold', 'total'):
            duration = value['cold'] if row['loop'] == 'cold' else value['samples']['total'][row['sample']]
            if duration != row['end_ns'] - row['start_ns']:
                raise ValueError('telemetry not from the measured transaction')
    if any(len(value['samples'][phase]) != samples for phase in ('input', 'solve', 'query', 'close', 'total')):
        raise ValueError('incomplete timing samples')
    positions = list(range(0, samples, CALIBRATION_EVERY)) + [samples] if samples else []
    if (data['calibration_every'], data['calibration_budget_ns']) != (CALIBRATION_EVERY, CALIBRATION_NS):
        raise ValueError('changed calibration protocol')
    if [(r['loop'], r['next_sample']) for r in data['calibration']] != [
            (loop, i) for loop in ('total', 'phases') for i in positions]:
        raise ValueError('missing/mislabelled calibration')
    for row in data['calibration']:
        if row['end_ns'] - row['start_ns'] < CALIBRATION_NS or row['iterations'] < 0:
            raise ValueError('invalid calibration')
        loop_rows = [r for r in rows if r['loop'] == row['loop']]
        i = row['next_sample']
        if (i and row['start_ns'] < loop_rows[i - 1]['end_ns']) or (
                i < samples and row['end_ns'] > loop_rows[i]['start_ns']):
            raise ValueError('calibration overlaps measured transaction')
    if any(event['start_ns'] > event['end_ns'] for event in data['collections']):
        raise ValueError('invalid GC interval')
