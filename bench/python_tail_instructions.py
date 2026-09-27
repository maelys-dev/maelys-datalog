#!/usr/bin/env python3
# SPDX-License-Identifier: MPL-2.0
"""Scoped Callgrind snapshots from the original hosted SDK binaries; no timings."""
import argparse
import ctypes
import json
import os
from pathlib import Path
import re
import subprocess
import sys

import python_perf as perf
import python_workload as workload

INDICES = (0, 50, 81, 100, 150, 200, 250, 300, 316, 330, 350, 370, 400, 421, 450, 500)
HEAD = 'f975be6a8e6778d320f48becce5d9e29c0573f6b'
CASES = ('7-integer-prepared', '93-integer-prepared')
HELPER = '''#include <valgrind/callgrind.h>
__attribute__((noinline)) void tail_begin(void) { CALLGRIND_ZERO_STATS; CALLGRIND_TOGGLE_COLLECT; }
__attribute__((noinline)) void tail_end(const char *name) { CALLGRIND_TOGGLE_COLLECT; CALLGRIND_DUMP_STATS_AT(name); }
'''


def worker(case, helper):
    lib = ctypes.CDLL(str(helper))
    lib.tail_begin.argtypes = []
    lib.tail_begin.restype = None
    lib.tail_end.argtypes = [ctypes.c_char_p]
    lib.tail_end.restype = None
    labels = {i: f'sample_{i:03d}'.encode() for i in INDICES}
    original = workload.repeat_transaction
    count = 0
    seen = []

    def wrap(transaction, repetitions):
        if repetitions != 1:
            raise ValueError('unexpected injection')
        def counted(phases=False):
            nonlocal count
            sample = count - 51  # First request + 50 warmups, then 501 total samples.
            count += 1
            label = labels.get(sample) if not phases else None
            if label is not None:
                lib.tail_begin()
                answer = transaction(phases)
                lib.tail_end(label)
                seen.append(sample)
                return answer
            return transaction(phases)
        return counted
    workload.repeat_transaction = wrap
    try:
        value = workload.measure(case, 501, 50)
    finally:
        workload.repeat_transaction = original
    if tuple(seen) != INDICES:
        raise ValueError('missing scoped transactions')
    # Valgrind timings are deliberately omitted; this is instruction evidence only.
    return dict(case=case, samples=seen, output_sha256=value['output_sha256'],
                release_eligible=False, interpretation='Callgrind software counts, not hardware cycles')


def parse_profile(path):
    text = path.read_text()
    trigger = re.search(r'^desc: Trigger: Client Request: sample_(\d+)$', text, re.M)
    if trigger is None:
        return None
    events = re.search(r'^events: (.+)$', text, re.M).group(1).split()
    values = [int(x) for x in re.search(r'^summary: (.+)$', text, re.M).group(1).split()]
    totals = dict(zip(events, values))
    if any(totals.get(k, 0) <= 0 for k in ('Ir', 'Dr', 'Dw')):
        raise ValueError('missing or empty software counters')
    return dict(sample=int(trigger.group(1)), counts={k: totals[k] for k in ('Ir', 'Dr', 'Dw')})


def run(evidence, output):
    evidence, output = evidence.resolve(), output.resolve()
    source_report = json.loads((evidence / 'report.json').read_text())
    if perf.sha256(evidence / 'report.json') != 'eaf17805d89eb4ddb2ee14f3cd7eb791d72a5b2f63efe7d032c60002650bee31':
        raise ValueError('wrong original report hash')
    if source_report['commits']['head'] != HEAD or source_report['commits']['anchor'] != perf.ANCHOR:
        raise ValueError('wrong original evidence')
    output.mkdir(parents=True, exist_ok=False)
    helper_source = output / 'helper.c'
    helper_source.write_text(HELPER)
    helper = output / 'helper.so'
    subprocess.run(['cc', '-shared', '-fPIC', '-O2', '-g', str(helper_source), '-o', str(helper)], check=True)
    report = dict(schema=1, release_eligible=False, indices=INDICES, cases=CASES, results=[],
                  source_report_sha256=perf.sha256(evidence / 'report.json'),
                  original_run=source_report['environment']['run_id'],
                  harness_sha256=perf.sha256(Path(__file__)), helper_sha256=perf.sha256(helper),
                  run_id=os.getenv('GITHUB_RUN_ID'),
                  environment=dict(python=sys.version, valgrind=perf.command(['valgrind', '--version'])))
    for name, args in (('cpu.txt', ['lscpu']), ('kernel.txt', ['uname', '-a'])):
        (output / name).write_text(perf.command(args) + '\n')
    for role, commit in (('base', perf.ANCHOR), ('head', HEAD)):
        sdk = evidence / commit / 'SMALL-Release'
        for name, digest in source_report['binaries'][f'{commit}/SMALL-Release'].items():
            if perf.sha256(sdk / name) != digest:
                raise ValueError('original binary/consumer hash mismatch')
    # All preparation precedes counters; repeat each fixture in opposite revision order.
    for repeat in range(2):
        for role, commit in ((('base', perf.ANCHOR), ('head', HEAD)) if repeat == 0 else
                             (('head', HEAD), ('base', perf.ANCHOR))):
            for case in CASES:
                target = output / f'{role}-{case}-{repeat}'
                target.mkdir()
                env = dict(os.environ, PYTHONPATH=str(evidence / commit / 'SMALL-Release/python'),
                           PYTHONHASHSEED='0', PYTHONDONTWRITEBYTECODE='1')
                with (target / 'consumer.json').open('w') as stdout, (target / 'valgrind.log').open('w') as stderr:
                    subprocess.run(['valgrind', '--tool=callgrind', '--collect-atstart=no',
                                    '--cache-sim=yes', '--error-exitcode=9',
                                    f'--callgrind-out-file={target}/callgrind.out',
                                    sys.executable, '-B', str(Path(__file__).resolve()),
                                    '--worker', case, '--helper', str(helper)],
                                   env=env, stdout=stdout, stderr=stderr, check=True)
                consumer = json.loads((target / 'consumer.json').read_text())
                if consumer['output_sha256'] != source_report['outputs'][case]:
                    raise ValueError('different answers under Callgrind')
                profiles = []
                for path in sorted(target.glob('callgrind.out*')):
                    item = parse_profile(path)
                    if item is None:
                        continue
                    item['file'] = str(path.relative_to(output))
                    annotation = perf.command(['callgrind_annotate', '--auto=no', '--inclusive=no',
                                               '--show=Ir,Dr,Dw', '--show-percs=no', '--threshold=100', str(path)])
                    path.with_suffix(path.suffix + '.txt').write_text(annotation + '\n')
                    profiles.append(item)
                if sorted(x['sample'] for x in profiles) != list(INDICES):
                    raise ValueError('incomplete Callgrind profile matrix')
                report['results'].append(dict(role=role, commit=commit, case=case, repeat=repeat, profiles=profiles))
                print('counted', role, case, repeat, flush=True)
    (output / 'report.json').write_text(json.dumps(report, indent=2) + '\n')
    print('instruction evidence', perf.sha256(output / 'report.json'), flush=True)


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--worker', choices=CASES)
    parser.add_argument('--helper', type=Path)
    parser.add_argument('--evidence', type=Path)
    parser.add_argument('--output', type=Path)
    args = parser.parse_args()
    if args.worker:
        if not args.helper:
            parser.error('--worker needs --helper')
        print(json.dumps(worker(args.worker, args.helper)))
    else:
        if not args.evidence or not args.output:
            parser.error('--evidence and --output required')
        run(args.evidence, args.output)
