#!/usr/bin/env python3
# SPDX-License-Identifier: MPL-2.0
"""Bounded v0.13 attribution using the original, hash-checked hosted binaries.

This is instruction evidence, not another timing vote or release gate.
"""
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
from python_tail_instructions import HELPER

REPORT_SHA = '57ad969645ca156262f798346267a077f65e8467a62c3c0e2bdb12b67573184a'
HEAD = '21ca6813936590410d3d774de078cd158d976fd5'
BASE = 'e8bfd813789147daf02dbbe5b830df7158e0af41'
FIXTURES = (
    ('SMALL-default', '7-integer-prepared', ('base', 'head')),
    ('LARGE-default', '7-symbol-prepared', ('base', 'head')),
    ('LARGE-Release', '7-symbol-prepared', ('base', 'head', 'anchor')),
    ('LARGE-Release', '93-integer-prepared', ('base', 'head')),
)
# First request, stable samples and the original burst's boundaries/interior.
INDICES = (0, 100, 421, 422, 436, 448, 449, 500)
LABELS = ('cold',) + tuple(f'total_{i:03d}' for i in INDICES)


def original_report(evidence):
    if perf.sha256(evidence / 'report.json') != REPORT_SHA:
        raise ValueError('wrong original report')
    report = json.loads((evidence / 'report.json').read_text())
    if report['commits']['head'] != HEAD or report['commits']['base'] != BASE:
        raise ValueError('wrong revisions')
    for name, digest in report['harness'].items():
        if perf.sha256(perf.ROOT / name) != digest:
            raise ValueError(f'changed original workload: {name}')
    for config, _, roles in FIXTURES:
        for role in roles:
            commit = report['commits'][role]
            for name, digest in report['binaries'][f'{commit}/{config}'].items():
                if perf.sha256(evidence / commit / config / name) != digest:
                    raise ValueError(f'changed original consumer: {commit}/{config}/{name}')
    return report


def worker(case, helper, loader=False):
    if not loader:
        lib = ctypes.CDLL(str(helper))
        lib.tail_begin.argtypes = []
        lib.tail_begin.restype = None
        lib.tail_end.argtypes = [ctypes.c_char_p]
        lib.tail_end.restype = None
    original = workload.repeat_transaction
    count = 0
    seen = []
    labels = {0: b'cold', **{i + 51: f'total_{i:03d}'.encode() for i in INDICES}}

    def wrap(transaction, repetitions):
        if repetitions != 1:
            raise ValueError('unexpected injection')
        def counted(phases=False):
            nonlocal count
            label = labels.get(count) if not phases else None
            count += 1
            if label is None:
                return transaction(phases)
            if loader:
                os.write(2, b'ATTRIBUTION_REQUEST_BEGIN\n')
                answer = transaction(phases)
                os.write(2, b'ATTRIBUTION_REQUEST_END\n')
            else:
                lib.tail_begin()
                answer = transaction(phases)
                lib.tail_end(label)
            seen.append(label.decode())
            return answer
        return counted
    workload.repeat_transaction = wrap
    try:
        value = workload.measure(case, 0 if loader else 501, 50, telemetry=True)
    finally:
        workload.repeat_transaction = original
    if seen != (['cold'] if loader else list(LABELS)):
        raise ValueError('missing scoped transactions')
    return dict(case=case, scopes=seen, output_sha256=value['output_sha256'],
                release_eligible=False, dlopen_flags=sys.getdlopenflags(),
                rtld_now=os.RTLD_NOW, rtld_lazy=os.RTLD_LAZY,
                interpretation='Software counts only; no Valgrind latency or hardware cache claims')


def parse_profile(path):
    value = path.read_text()
    match = re.search(r'^desc: Trigger: Client Request: (cold|total_\d+)$', value, re.M)
    if not match:
        return None
    events = re.search(r'^events: (.+)$', value, re.M).group(1).split()
    totals = list(map(int, re.search(r'^summary: (.+)$', value, re.M).group(1).split()))
    counts = dict(zip(events, totals))
    if len(events) != len(totals) or any(counts.get(k, 0) <= 0 for k in ('Ir', 'Dr', 'Dw')):
        raise ValueError('incomplete software counters')
    return dict(scope=match.group(1), counts={k: counts[k] for k in ('Ir', 'Dr', 'Dw')})


def run(evidence, output):
    evidence, output = evidence.resolve(), output.resolve()
    source = original_report(evidence)
    output.mkdir(parents=True, exist_ok=False)
    helper_source = output / 'helper.c'
    helper_source.write_text(HELPER)
    helper = output / 'helper.so'
    subprocess.run(['cc', '-shared', '-fPIC', '-O2', '-g', str(helper_source), '-o', str(helper)], check=True)
    report = dict(schema=1, release_eligible=False, original_run=36331650860,
                  source_report_sha256=REPORT_SHA, fixtures=FIXTURES, scopes=LABELS,
                  harness_sha256=perf.sha256(Path(__file__)), helper_sha256=perf.sha256(helper),
                  environment=dict(python=sys.version, valgrind=perf.command(['valgrind', '--version']),
                                   host=perf.host_description()), results=[], loader=[])
    # Preserve the actual linked layouts, not just four compiler objects.
    for config, role in sorted({(config, role) for config, _, roles in FIXTURES for role in roles}):
        folder = output / 'linked-layout' / f'{config}-{role}'
        folder.mkdir(parents=True)
        package = evidence / source['commits'][role] / config / 'python/maelys_datalog'
        for binary in sorted(package.glob('*.so')):
            (folder / (binary.name + '.readelf.txt')).write_text(
                perf.command(['readelf', '-W', '-S', '-d', '-s', '-r', binary]) + '\n')
            (folder / (binary.name + '.objdump.txt')).write_text(
                perf.command(['objdump', '-d', '-w', binary]) + '\n')
    for repeat in range(2):
        for config, case, roles in FIXTURES:
            for role in roles if repeat == 0 else reversed(roles):
                commit = source['commits'][role]
                target = output / f'{config}-{case}-{role}-{repeat}'
                target.mkdir()
                env = dict(os.environ, PYTHONPATH=str(evidence / commit / config / 'python'),
                           PYTHONHASHSEED='0', PYTHONDONTWRITEBYTECODE='1')
                cmd = [sys.executable, '-B', str(Path(__file__).resolve()), '--worker', case, '--helper', str(helper)]
                with (target / 'consumer.json').open('w') as stdout, (target / 'valgrind.log').open('w') as stderr:
                    subprocess.run(['valgrind', '--tool=callgrind', '--collect-atstart=no', '--cache-sim=yes',
                                    '--error-exitcode=9', f'--callgrind-out-file={target}/callgrind.out'] + cmd,
                                   env=env, stdout=stdout, stderr=stderr, check=True)
                consumer = json.loads((target / 'consumer.json').read_text())
                if consumer['output_sha256'] != source['outputs'][case]:
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
                if sorted(p['scope'] for p in profiles) != sorted(LABELS):
                    raise ValueError('incomplete scope matrix')
                report['results'].append(dict(config=config, case=case, role=role, repeat=repeat,
                                              commit=commit, consumer=consumer, profiles=profiles))
                print('counted', config, case, role, repeat, flush=True)
                # Separate untimed loader trace. Mark the exact first transaction;
                # imports, setup and shutdown are outside those markers.
                if repeat == 0 and case == '7-symbol-prepared':
                    with (target / 'loader.json').open('w') as stdout, (target / 'loader.log').open('w') as stderr:
                        subprocess.run(cmd + ['--loader'], env=dict(env, LD_DEBUG='bindings'),
                                       stdout=stdout, stderr=stderr, check=True)
                    lines = (target / 'loader.log').read_text().splitlines()
                    start, end = lines.index('ATTRIBUTION_REQUEST_BEGIN'), lines.index('ATTRIBUTION_REQUEST_END')
                    record = json.loads((target / 'loader.json').read_text())
                    if record['output_sha256'] != source['outputs'][case]:
                        raise ValueError('different answers in loader trace')
                    report['loader'].append(dict(config=config, role=role, flags=record['dlopen_flags'],
                                                 request_bindings=lines[start + 1:end]))
                (output / 'report.json').write_text(json.dumps(report, indent=2) + '\n')
    text = ['# v0.13 Python instruction attribution', '',
            'Original run: 36331650860. Original binaries verified; no rebuilding or retiming.',
            'Software counts cover whole transactions, excluding setup/clocks/answer checks.',
            'Instrumentation call boundaries remain; inspect per-function profiles, not only totals.',
            'These data cannot retrospectively measure cycles or attribute an old transient.', '',
            '| Configuration | Case | Revision | Repeat | Scope | Ir | Dr | Dw |',
            '|---|---|---|---:|---|---:|---:|---:|']
    for row in report['results']:
        for p in row['profiles']:
            text.append(f"| {row['config']} | {row['case']} | {row['role']} | {row['repeat']} | {p['scope']} | " +
                        ' | '.join(str(p['counts'][k]) for k in ('Ir', 'Dr', 'Dw')) + ' |')
    (output / 'report.md').write_text('\n'.join(text) + '\n')


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--worker', choices=workload.CASES)
    parser.add_argument('--helper', type=Path)
    parser.add_argument('--loader', action='store_true')
    parser.add_argument('--evidence', type=Path)
    parser.add_argument('--output', type=Path)
    args = parser.parse_args()
    if args.worker:
        if not args.helper:
            parser.error('--worker needs --helper')
        print(json.dumps(worker(args.worker, args.helper, args.loader)))
    else:
        if not args.evidence or not args.output:
            parser.error('--evidence and --output required')
        run(args.evidence, args.output)
