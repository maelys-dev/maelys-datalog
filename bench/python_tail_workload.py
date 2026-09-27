#!/usr/bin/env python3
# SPDX-License-Identifier: MPL-2.0
"""Separate diagnostic observer; never substitutes for release timing samples."""
import argparse
from array import array
import gc
import json
import resource
import time

import python_workload as workload

FIELDS = ('start_ns', 'wall_ns', 'thread_ns', 'voluntary', 'involuntary',
          'minor_faults', 'major_faults')
USAGE = getattr(resource, 'RUSAGE_THREAD', resource.RUSAGE_SELF)


class Observer:
    def __init__(self, capacity):
        self.capacity = capacity
        self.count = 0
        # Preallocate untracked numeric storage; no growing per-request records.
        self.values = array('q', [0]) * (capacity * len(FIELDS))
        self.collections = []
        self.gc_start = None

    def gc_event(self, phase, info):
        if phase == 'start':
            self.gc_start = (time.perf_counter_ns(), info['generation'])
        elif self.gc_start is not None:
            start, generation = self.gc_start
            end = time.perf_counter_ns()
            self.collections.append(dict(start_ns=start, end_ns=end,
                                         generation=generation, collected=info['collected']))
            self.gc_start = None

    def wrap(self, transaction, repetitions):
        if repetitions != 1:
            raise ValueError('diagnostic cannot inject repeated requests')

        def observed(phases=False):
            if self.count >= self.capacity:
                raise ValueError('observer storage exhausted')
            before = resource.getrusage(USAGE)
            cpu_start = time.thread_time_ns()
            start = time.perf_counter_ns()
            answer = transaction(phases)
            end = time.perf_counter_ns()
            cpu_end = time.thread_time_ns()
            after = resource.getrusage(USAGE)
            offset = self.count * len(FIELDS)
            self.values[offset] = start
            self.values[offset + 1] = end - start
            self.values[offset + 2] = cpu_end - cpu_start
            self.values[offset + 3] = after.ru_nvcsw - before.ru_nvcsw
            self.values[offset + 4] = after.ru_nivcsw - before.ru_nivcsw
            self.values[offset + 5] = after.ru_minflt - before.ru_minflt
            self.values[offset + 6] = after.ru_majflt - before.ru_majflt
            self.count += 1
            return answer
        return observed

    def records(self):
        return [dict(zip(FIELDS, self.values[i * len(FIELDS):(i + 1) * len(FIELDS)]))
                for i in range(self.count)]


def measure(case, mode, samples=501, warmup=50):
    if mode not in ('plain', 'observe', 'gc-off'):
        raise ValueError('unknown diagnostic mode')
    enabled = gc.isenabled()
    original = workload.repeat_transaction
    observer = Observer(1 + warmup + 2 * samples) if mode != 'plain' else None
    try:
        if observer:
            gc.callbacks.append(observer.gc_event)
        if mode == 'gc-off':
            gc.disable()
        # Plain keeps the exact original callable and workload, including clocks.
        workload.repeat_transaction = observer.wrap if observer else original
        result = workload.measure(case, samples, warmup)
    finally:
        workload.repeat_transaction = original
        if observer:
            gc.callbacks.remove(observer.gc_event)
        gc.enable() if enabled else gc.disable()
    result.update(mode=mode, release_eligible=False,
                  usage_scope='thread' if hasattr(resource, 'RUSAGE_THREAD') else 'process',
                  clocks={name: vars(time.get_clock_info(name))
                          for name in ('perf_counter', 'thread_time')})
    if observer:
        if observer.count != observer.capacity:
            raise ValueError('missing observed transactions')
        result.update(observed=observer.records(), collections=observer.collections,
                      total_range=[1 + warmup, 1 + warmup + samples],
                      phase_range=[1 + warmup + samples, observer.count])
    return result


def selfcheck():
    """Known waits, CPU work and GC, outside all consumer measurements."""
    observer = Observer(3)
    observer.wrap(lambda _: time.sleep(.004), 1)()

    def cpu_work(_):
        stop = time.thread_time_ns() + 4000000
        while time.thread_time_ns() < stop:
            pass
    observer.wrap(cpu_work, 1)()
    gc.callbacks.append(observer.gc_event)
    try:
        observer.wrap(lambda _: gc.collect(0), 1)()
    finally:
        gc.callbacks.remove(observer.gc_event)
    rows = observer.records()
    if rows[0]['wall_ns'] - rows[0]['thread_ns'] < 2000000:
        raise ValueError('observer missed known wait')
    if rows[1]['thread_ns'] < 4000000:
        raise ValueError('observer missed known CPU work')
    start, end = rows[2]['start_ns'], rows[2]['start_ns'] + rows[2]['wall_ns']
    if not any(start <= event['start_ns'] <= event['end_ns'] <= end
               for event in observer.collections):
        raise ValueError('observer missed known collection')
    return dict(records=rows, collections=observer.collections)


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('case', choices=workload.CASES)
    parser.add_argument('--mode', choices=('plain', 'observe', 'gc-off'), required=True)
    parser.add_argument('--samples', type=int, default=501)
    parser.add_argument('--warmup', type=int, default=50)
    args = parser.parse_args()
    print(json.dumps(measure(args.case, args.mode, args.samples, args.warmup)))
