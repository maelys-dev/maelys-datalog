#!/usr/bin/env python3
# SPDX-License-Identifier: MPL-2.0
"""Bounded #147 follow-up of the original complete-Python release report.

Archived installed consumers, independent same-path process labels and scoped
software counts. Never rebuild a measured binary or replace a release verdict.
"""
import argparse
import ctypes
import importlib.metadata
import json
import os
from pathlib import Path
import shutil
import statistics
import subprocess
import sys

import python_perf as perf
import python_workload as workload
from python_tail_instructions import HELPER
from python_prepared_diagnostic import scoped_counts

SOURCE_RUN = '36678625484'
SOURCE_SHA256 = '671c0431ffe4272fddd53876f210dc48a0da5ccade8e6ea6b758e48fb791c2e4'
COMMITS = {'base': '9700d3802feb98d46821f908be34d895e22de55e',
           'head': 'b0cffed82bfa56f090870172777496fd7a0e1f45'}
# Declared before this campaign: largest total tail, largest prepared median,
# prepared symbol median, and repeated SMALL/default input/solve phase alerts.
FIXTURES = (('LARGE-default', '93-integer-solve'),
            ('LARGE-default', '7-integer-prepared'),
            ('LARGE-Release', '93-symbol-prepared'),
            ('SMALL-default', '93-integer-prepared'))
ROLES = ('base', 'head', 'base_copy', 'head_copy')
ORDERS = tuple(order for i in range(4)
               for order in (ROLES[i:] + ROLES[:i], tuple(reversed(ROLES[i:] + ROLES[:i]))))
INDICES = (0, 100, 320, 403, 412, 436, 449, 500)


def schedule():
    return [(role, f'aa{i}') for role in ROLES for i in range(4)] + [
        (role, f'ab{i}') for i, order in enumerate(ORDERS) for role in order]


def originals(root):
    if perf.sha256(root / 'report.json') != SOURCE_SHA256:
        raise ValueError('wrong original report digest')
    report = json.loads((root / 'report.json').read_text())
    if (report['schema'] != 4 or report['environment']['run_id'] != SOURCE_RUN or
            not report['release_eligible'] or report['comparison_only'] or
            not report['positive_control']['detected'] or
            any(report['commits'][r] != c for r, c in COMMITS.items())):
        raise ValueError('wrong original revision/protocol')
    if report['harness'] != {f: perf.sha256(perf.ROOT / f) for f in perf.HARNESS_FILES}:
        raise ValueError('changed complete request harness')
    if sys.version.split()[0] != report['environment']['python'].split()[0]:
        raise ValueError('different Python interpreter version')
    dependencies = {k: importlib.metadata.version(k) for k in report['environment']['packages']}
    if dependencies != report['environment']['packages']:
        raise ValueError('different Python dependency versions')
    packages = {}
    for role in ROLES:
        commit = COMMITS[role.removesuffix('_copy')]
        for config, _ in FIXTURES:
            if (role, config) in packages:
                continue
            folder = root / commit / config
            for name, digest in report['binaries'][f'{commit}/{config}'].items():
                if perf.sha256(folder / name) != digest:
                    raise ValueError('changed original binary/header/binding: ' + name)
            packages[role, config] = folder / 'python'
    return report, packages


def worker(case, helper):
    lib = ctypes.CDLL(str(helper))
    lib.tail_begin.argtypes, lib.tail_begin.restype = [], None
    lib.tail_end.argtypes, lib.tail_end.restype = [ctypes.c_char_p], None
    original = workload.repeat_transaction
    calls, seen = 0, []
    labels = {i: f'total_{i:03d}'.encode() for i in INDICES}

    def wrap(transaction, repetitions):
        if repetitions != 1:
            raise ValueError('unexpected request injection')
        def request(phases=False):
            nonlocal calls
            index = calls - 51  # cold + 50 warmups precede the total loop
            calls += 1
            if phases or index not in labels:
                return transaction(phases)
            lib.tail_begin()
            answer = transaction(phases)
            lib.tail_end(labels[index])
            seen.append(labels[index].decode())
            return answer
        return request
    workload.repeat_transaction = wrap
    try:
        value = workload.measure(case, 501, 50, telemetry=True)
    finally:
        workload.repeat_transaction = original
    if seen != [f'total_{i:03d}' for i in INDICES]:
        raise ValueError('missing or mislabelled complete-request counts')
    # No instrumented duration escapes this separate worker.
    return dict(scopes=seen, output_sha256=value['output_sha256'])


def comparisons(samples):
    rows = []
    for base, head in (('base', 'head'), ('base', 'base_copy'),
                       ('head', 'head_copy'), ('base_copy', 'head_copy')):
        for scenario in sorted(samples[base]['aa0']):
            reference = [v for i in range(4) for v in samples[base][f'aa{i}'][scenario]]
            metrics = ('min',) if statistics.median(reference) < 10000 else ('median', 'p95')
            for metric in metrics:
                floor = max(abs(perf.statistic(samples[role][f'aa{i+1}'][scenario], metric) /
                                perf.statistic(samples[role][f'aa{i}'][scenario], metric) - 1)
                            for role in (base, head) for i in (0, 2))
                delta = [perf.statistic(samples[head][f'ab{i}'][scenario], metric) /
                         perf.statistic(samples[base][f'ab{i}'][scenario], metric) - 1
                         for i in range(len(ORDERS))]
                rows.append(dict(base=base, head=head, scenario=scenario, metric=metric,
                                 aa_floor=floor, delta=delta,
                                 classification=['slower' if d > floor else 'faster' if d < -floor
                                                 else 'indeterminate' for d in delta]))
    return rows


def run(root, output):
    if any(v for k, v in os.environ.items() if k.startswith('MALLOC_') or
           k in ('GLIBC_TUNABLES', 'LD_PRELOAD')):
        raise ValueError('inherited allocator intervention')
    source, packages = originals(root.resolve())
    output = output.resolve()
    output.mkdir(parents=True, exist_ok=False)
    (output / 'raw').mkdir()
    shutil.copy2(root / 'report.json', output / 'original-report.json')
    helper = output / 'helper.so'
    (output / 'helper.c').write_text(HELPER)
    subprocess.run(['cc', '-shared', '-fPIC', '-O2', '-g', str(output / 'helper.c'), '-o', str(helper)], check=True)
    report = dict(schema=1, release_eligible=False, complete=False,
                  source_run=SOURCE_RUN, source_report_sha256=SOURCE_SHA256,
                  commits=COMMITS, fixtures=FIXTURES, schedule=schedule(), count_indices=INDICES,
                  packages={f'{r}/{c}': str(p) for (r, c), p in packages.items()},
                  environment=dict(host=perf.host_description(), python=sys.version,
                                   packages=source['environment']['packages'],
                                   libc=perf.command(['ldd', '--version']),
                                   valgrind=perf.command(['valgrind', '--version']),
                                   run_id=os.getenv('GITHUB_RUN_ID'), run_attempt=os.getenv('GITHUB_RUN_ATTEMPT')),
                  tooling_commit=perf.command(['git', 'rev-parse', 'HEAD']),
                  tooling_hashes={f: perf.sha256(perf.ROOT / 'bench' / f) for f in
                                  ('python_javascript_diagnostic.py', 'python_tail_instructions.py',
                                   'python_prepared_diagnostic.py', 'report_host_delta.py')},
                  workload_hashes=source['harness'],
                  helper_sha256=perf.sha256(helper), samples={}, counts=[],
                  limits='Same-path labels measure process variability, not binary placement. Software counts are not cycles and cannot retrospectively explain the original tail. Exclusive function names aggregate across objects; unresolved names and summary residuals remain visible. No timing classification is removed or used as release approval.')
    # Capture original linked code; every build/preparation finishes before time.
    for (role, config), package in packages.items():
        if role.endswith('_copy'):
            continue
        folder = output / 'linked-layout' / f'{role}-{config}'
        folder.mkdir(parents=True)
        for binary in (package / 'maelys_datalog').glob('*.so'):
            (folder / (binary.name + '.readelf')).write_text(perf.command(['readelf', '-W', '-S', '-s', '-d', binary]) + '\n')
            (folder / (binary.name + '.asm')).write_text(perf.command(['objdump', '-d', '-w', binary]) + '\n')
    def save():
        (output / 'report.json').write_text(json.dumps(report, indent=2) + '\n')
    save()
    samples = {role: {pas: {} for r, pas in schedule() if r == role} for role in ROLES}
    for config, case in FIXTURES:
        for role, pas in schedule():
            env = dict(os.environ, PYTHONPATH=str(packages[role, config]),
                       PYTHONHASHSEED='0', PYTHONDONTWRITEBYTECODE='1')
            value = json.loads(perf.command([sys.executable, '-B', perf.ROOT / 'bench/python_workload.py',
                                            case, '--samples', 501, '--warmup', 50, '--telemetry'], env=env))
            perf.validate_telemetry(value, 501, 50)
            if value['request_repetitions'] != 1 or value['output_sha256'] != source['outputs'][case]:
                raise ValueError('different complete-request answers')
            (output / 'raw' / f'{config}-{case}-{role}-{pas}.json').write_text(json.dumps(value) + '\n')
            for phase in perf.PHASES:
                samples[role][pas][f'{config}/{case}/{phase}'] = value['samples'][phase]
        print('timed', config, case, flush=True)
    report['samples'], report['rows'] = samples, comparisons(samples)
    save()
    # Fresh instrumented processes, reversed order on repeat, no Valgrind timing.
    for repeat in range(2):
        for config, case in FIXTURES:
            for role in (('base', 'head') if repeat == 0 else ('head', 'base')):
                target = output / 'counts' / f'{config}-{case}-{role}-{repeat}'
                target.mkdir(parents=True)
                env = dict(os.environ, PYTHONPATH=str(packages[role, config]),
                           PYTHONHASHSEED='0', PYTHONDONTWRITEBYTECODE='1')
                with (target / 'consumer.json').open('w') as stdout, (target / 'valgrind.log').open('w') as stderr:
                    subprocess.run(['valgrind', '--tool=callgrind', '--collect-atstart=no', '--cache-sim=yes',
                                    '--error-exitcode=9', f'--callgrind-out-file={target}/callgrind.out',
                                    sys.executable, '-B', str(Path(__file__).resolve()), '--worker', case,
                                    '--helper', str(helper)], env=env, stdout=stdout, stderr=stderr, check=True)
                expected = dict(scopes=[f'total_{i:03d}' for i in INDICES], output_sha256=source['outputs'][case])
                if json.loads((target / 'consumer.json').read_text()) != expected:
                    raise ValueError('different counted answers/scopes')
                profiles = []
                for path in sorted(target.glob('callgrind.out*')):
                    item = scoped_counts(path)
                    if item is not None:
                        profiles.append(dict(item, file=str(path.relative_to(output))))
                if sorted(p['label'] for p in profiles) != expected['scopes']:
                    raise ValueError('incomplete scoped count matrix')
                report['counts'].append(dict(config=config, case=case, role=role, repeat=repeat, profiles=profiles))
                save()
                print('counted', config, case, role, repeat, flush=True)
    report['complete'] = True
    save()
    lines = ['# JavaScript release: bounded Python investigation', '',
             'Original #147 installed consumers, four declared fixtures, independent identical-binary labels.',
             'All observations retained; scoped software counts are separate from timing.',
             'Original release report and review decision remain unchanged.', '',
             '| Comparison | Scenario | Statistic | A/A % | Eight round deltas % |',
             '| --- | --- | --- | --- | --- |']
    for row in report['rows']:
        if row['scenario'].endswith('/total'):
            lines.append(f"| {row['head']}/{row['base']} | {row['scenario']} | {row['metric']} | "
                         f"{100*row['aa_floor']:.3f} | " + ', '.join(f'{100*d:+.3f}' for d in row['delta']) + ' |')
    (output / 'report.md').write_text('\n'.join(lines) + '\n')


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--evidence', type=Path)
    parser.add_argument('--output', type=Path)
    parser.add_argument('--worker', choices=workload.CASES)
    parser.add_argument('--helper', type=Path)
    args = parser.parse_args()
    if args.worker:
        if not args.helper or args.evidence or args.output:
            parser.error('--worker needs only --helper')
        print(json.dumps(worker(args.worker, args.helper)))
    else:
        if not args.evidence or not args.output or args.helper:
            parser.error('--evidence and --output required')
        run(args.evidence, args.output)
