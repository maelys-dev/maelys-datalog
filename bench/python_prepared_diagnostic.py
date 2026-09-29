#!/usr/bin/env python3
# SPDX-License-Identifier: MPL-2.0
"""Fixed prepared-request investigation using the archived #143 consumers.

Two independent instruments: ordinary wall/CPU/resource/GC observations and
scoped exclusive Callgrind counts. Neither rewrites the original reports.
"""
import argparse
import ctypes
import importlib.metadata
import json
import os
from pathlib import Path
import statistics
import shutil
import subprocess
import sys

import python_perf as perf
import python_workload as workload
from python_tail_instructions import HELPER
from report_host_delta import counts

SOURCES = {
    'release': (36545968747, 'a73b1cfc0ff1927776f107f6ea489a4800d5d8d3316d10b4b5518be4bcffc4b7'),
    'parent': (36545921158, '4f5862024c6c7a2cf6e9bda548d66350319f3a4a8747a75fbc94613254cdad6c'),
}
COMMITS = {'base': '43bbde637435e5f6fb4756fb22c25f18f8f30583',
           'parent': '41c8857838beaa30ba845381dbc7d40d7d11cc46',
           'head': '646ac3d76916bc20a990bf08deea37fea4c56fde'}
FIXTURES = (('SMALL-default', '93-integer-prepared'),
            ('SMALL-Release', '93-integer-prepared'),
            ('SMALL-default', '7-symbol-prepared'),
            ('LARGE-Release', '7-symbol-prepared'),
            ('SMALL-Release', '93-symbol-prepared'))
ROLES = ('base', 'parent', 'head', 'head_copy')
ORDERS = tuple(order for i in range(4)
               for order in (ROLES[i:] + ROLES[:i], tuple(reversed(ROLES[i:] + ROLES[:i]))))
INDICES = (0, 100, 192, 320, 500)


def schedule():
    return [(role, f'aa{i}') for role in ROLES for i in range(4)] + [
        (role, f'ab{i}') for i, order in enumerate(ORDERS) for role in order]


def originals(roots):
    reports = {}
    for name, (run, digest) in SOURCES.items():
        root = roots[name]
        if perf.sha256(root / 'report.json') != digest:
            raise ValueError('wrong original report: ' + name)
        r = json.loads((root / 'report.json').read_text())
        if (r['commits']['head'] != COMMITS['head'] or
                r['commits']['base'] != COMMITS['base' if name == 'release' else 'parent'] or
                r['environment']['run_id'] != str(run) or not r['positive_control']['detected']):
            raise ValueError('wrong original revision/protocol')
        if r['harness'] != {f: perf.sha256(perf.ROOT / f) for f in perf.HARNESS_FILES}:
            raise ValueError('changed complete request harness')
        reports[name] = r
    packages = {}
    for role in ROLES:
        source = 'parent' if role == 'parent' else 'release'
        commit = COMMITS['head' if role == 'head_copy' else role]
        for config, _ in FIXTURES:
            if (role, config) in packages:
                continue
            folder = roots[source] / commit / config
            for name, digest in reports[source]['binaries'][f'{commit}/{config}'].items():
                if perf.sha256(folder / name) != digest:
                    raise ValueError('changed original binary/header/binding: ' + name)
            packages[role, config] = folder / 'python'
    return reports, packages


def worker(case, helper):
    lib = ctypes.CDLL(str(helper))
    lib.tail_begin.argtypes = []
    lib.tail_begin.restype = None
    lib.tail_end.argtypes = [ctypes.c_char_p]
    lib.tail_end.restype = None
    original = workload.repeat_transaction
    calls = 0
    seen = []
    labels = {i: f'total_{i:03d}'.encode() for i in INDICES}

    def wrap(transaction, repetitions):
        if repetitions != 1:
            raise ValueError('unexpected request injection')
        def request(phases=False):
            nonlocal calls
            index = calls - 51
            calls += 1
            if phases or index not in INDICES:
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
    # No Valgrind times escape this separate instrument.
    return dict(scopes=seen, output_sha256=value['output_sha256'])


def comparisons(samples):
    rows = []
    for base, head in (('base', 'head'), ('parent', 'head'), ('head', 'head_copy')):
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


def scoped_counts(path):
    # Callgrind's final unrequested dump can have the abbreviated `summary: 0`.
    # Keep that file, but parse only explicit client-request regions.
    if not any(line.startswith('desc: Trigger: Client Request: ')
               for line in path.read_text().splitlines()):
        return None
    return counts(path)


def run(release, parent, output):
    if any(v for k, v in os.environ.items() if k.startswith('MALLOC_') or
           k in ('GLIBC_TUNABLES', 'LD_PRELOAD')):
        raise ValueError('inherited allocator intervention')
    roots = dict(release=release.resolve(), parent=parent.resolve())
    source, packages = originals(roots)
    dependencies = {k: importlib.metadata.version(k) for k in source['release']['environment']['packages']}
    for original in source.values():
        if sys.version.split()[0] != original['environment']['python'].split()[0]:
            raise ValueError('different Python interpreter version')
        if dependencies != original['environment']['packages']:
            raise ValueError('different Python dependency versions')
    output = output.resolve()
    output.mkdir(parents=True, exist_ok=False)
    (output / 'raw').mkdir()
    for name, root in roots.items():
        shutil.copy2(root / 'report.json', output / f'original-{name}-report.json')
    helper = output / 'helper.so'
    (output / 'helper.c').write_text(HELPER)
    subprocess.run(['cc', '-shared', '-fPIC', '-O2', '-g', str(output / 'helper.c'), '-o', str(helper)], check=True)
    report = dict(schema=1, release_eligible=False, sources=SOURCES, commits=COMMITS,
                  fixtures=FIXTURES, schedule=schedule(), count_indices=INDICES,
                  environment=dict(host=perf.host_description(), python=sys.version, packages=dependencies,
                                   libc=perf.command(['ldd', '--version']),
                                   valgrind=perf.command(['valgrind', '--version']),
                                   run_id=os.getenv('GITHUB_RUN_ID')),
                  harness_sha256=perf.sha256(Path(__file__)),
                  workload_hashes=source['release']['harness'], helper_sha256=perf.sha256(helper),
                  attribution_limit='Exclusive function names are aggregated across objects; unresolved names and summary residuals remain visible. Counts are software events, not hardware cycles.',
                  samples={}, counts=[])
    # Verify and preserve linked code before any timing starts. No later builds.
    for (role, config), package in packages.items():
        if role == 'head_copy':
            continue
        folder = output / 'linked-layout' / f'{role}-{config}'
        folder.mkdir(parents=True)
        for binary in (package / 'maelys_datalog').glob('*.so'):
            (folder / (binary.name + '.readelf')).write_text(perf.command(['readelf', '-W', '-S', '-s', '-d', binary]) + '\n')
            (folder / (binary.name + '.asm')).write_text(perf.command(['objdump', '-d', '-w', binary]) + '\n')
    samples = {role: {pas: {} for r, pas in schedule() if r == role} for role in ROLES}
    for config, case in FIXTURES:
        for role, pas in schedule():
            env = dict(os.environ, PYTHONPATH=str(packages[role, config]),
                       PYTHONHASHSEED='0', PYTHONDONTWRITEBYTECODE='1')
            value = json.loads(perf.command([sys.executable, '-B', perf.ROOT / 'bench/python_workload.py',
                                            case, '--samples', 501, '--warmup', 50, '--telemetry'], env=env))
            perf.validate_telemetry(value, 501, 50)
            if value['request_repetitions'] != 1 or value['output_sha256'] != source['release']['outputs'][case]:
                raise ValueError('different complete-request answers')
            (output / 'raw' / f'{config}-{case}-{role}-{pas}.json').write_text(json.dumps(value) + '\n')
            for phase in perf.PHASES:
                samples[role][pas][f'{config}/{case}/{phase}'] = value['samples'][phase]
        print('timed', config, case, flush=True)
    report['samples'] = samples
    report['rows'] = comparisons(samples)
    (output / 'report.json').write_text(json.dumps(report, indent=2) + '\n')
    # Separate processes/instrumentation; preparation, clocks and checks excluded.
    for repeat in range(2):
        for config, case in FIXTURES:
            for role in (('base', 'parent', 'head') if repeat == 0 else ('head', 'parent', 'base')):
                target = output / 'counts' / f'{config}-{case}-{role}-{repeat}'
                target.mkdir(parents=True)
                env = dict(os.environ, PYTHONPATH=str(packages[role, config]),
                           PYTHONHASHSEED='0', PYTHONDONTWRITEBYTECODE='1')
                with (target / 'consumer.json').open('w') as stdout, (target / 'valgrind.log').open('w') as stderr:
                    subprocess.run(['valgrind', '--tool=callgrind', '--collect-atstart=no', '--cache-sim=yes',
                                    '--error-exitcode=9', f'--callgrind-out-file={target}/callgrind.out',
                                    sys.executable, '-B', str(Path(__file__).resolve()), '--worker', case,
                                    '--helper', str(helper)], env=env, stdout=stdout, stderr=stderr, check=True)
                consumer = json.loads((target / 'consumer.json').read_text())
                if consumer['output_sha256'] != source['release']['outputs'][case]:
                    raise ValueError('different answers under Callgrind')
                profiles = []
                for path in sorted(target.glob('callgrind.out*')):
                    item = scoped_counts(path)
                    if item is None:
                        continue
                    item['file'] = str(path.relative_to(output))
                    profiles.append(item)
                if sorted(x['label'] for x in profiles) != [f'total_{i:03d}' for i in INDICES]:
                    raise ValueError('incomplete scoped count matrix')
                report['counts'].append(dict(config=config, case=case, role=role, repeat=repeat, profiles=profiles))
                (output / 'report.json').write_text(json.dumps(report, indent=2) + '\n')
                print('counted', config, case, role, repeat, flush=True)
    lines = ['# Prepared-request investigation', '',
             'Original #143 consumers, unchanged workload, independent same-head labels.',
             'Two A/A pairs, eight balanced rounds; scoped software counts in separate processes.',
             'All observations retained. No retrospective attribution or release approval.', '',
             '| Comparison | Scenario | Metric | A/A % | Round deltas % |',
             '|---|---|---|---:|---|']
    for row in report['rows']:
        if row['scenario'].endswith('/total'):
            lines.append(f"| {row['head']}/{row['base']} | {row['scenario']} | {row['metric']} | "
                         f"{100*row['aa_floor']:.3f} | " + ', '.join(f'{100*d:+.3f}' for d in row['delta']) + ' |')
    (output / 'report.md').write_text('\n'.join(lines) + '\n')


SPAN_RUN = '36566987207'
SPAN_COMMITS = {'base': '1d6ca9534bbc8bedf47103555087d042fdf10c60',
                'head': 'adf2cf3d112efc95fbb9be0a5386346d630fb0bc'}


def span_packages(root, report_digest):
    if perf.sha256(root / 'report.json') != report_digest:
        raise ValueError('wrong fixed span report digest')
    report = json.loads((root / 'report.json').read_text())
    if (report['schema'] != 4 or report['environment']['run_id'] != SPAN_RUN or
            not report['comparison_only'] or report['release_eligible'] or
            any(report['commits'][role] != commit for role, commit in SPAN_COMMITS.items())):
        raise ValueError('wrong span comparison revisions/protocol')
    if report['harness'] != {f: perf.sha256(perf.ROOT / f) for f in perf.HARNESS_FILES}:
        raise ValueError('changed complete request harness')
    if sys.version.split()[0] != report['environment']['python'].split()[0]:
        raise ValueError('different Python interpreter version')
    dependencies = {k: importlib.metadata.version(k) for k in report['environment']['packages']}
    if dependencies != report['environment']['packages']:
        raise ValueError('different Python dependency versions')
    packages = {}
    for role, commit in SPAN_COMMITS.items():
        for config, _ in FIXTURES:
            folder = root / commit / config
            for name, digest in report['binaries'][f'{commit}/{config}'].items():
                if perf.sha256(folder / name) != digest:
                    raise ValueError('changed span binary/header/binding: ' + name)
            packages[role, config] = folder / 'python'
    return report, packages


def span_counts(root, report_digest, output):
    if any(v for k, v in os.environ.items() if k.startswith('MALLOC_') or
           k in ('GLIBC_TUNABLES', 'LD_PRELOAD')):
        raise ValueError('inherited allocator intervention')
    original, packages = span_packages(root.resolve(), report_digest)
    output = output.resolve()
    output.mkdir(parents=True, exist_ok=False)
    shutil.copy2(root / 'report.json', output / 'original-report.json')
    helper = output / 'helper.so'
    (output / 'helper.c').write_text(HELPER)
    subprocess.run(['cc', '-shared', '-fPIC', '-O2', '-g', str(output / 'helper.c'), '-o', str(helper)], check=True)
    report = dict(schema=1, release_eligible=False, source_run=SPAN_RUN,
                  source_report_sha256=report_digest, commits=SPAN_COMMITS, fixtures=FIXTURES,
                  count_indices=INDICES, helper_sha256=perf.sha256(helper),
                  harness_sha256=perf.sha256(Path(__file__)), workload_hashes=original['harness'],
                  environment=dict(host=perf.host_description(), python=sys.version,
                                   packages=original['environment']['packages'],
                                   libc=perf.command(['ldd', '--version']),
                                   valgrind=perf.command(['valgrind', '--version']),
                                   run_id=os.getenv('GITHUB_RUN_ID')),
                  counts=[], limits='Software counts only; complete-request timing remains in the original report. Exclusive names are aggregated across objects; unresolved names and summary residuals are retained.')
    for (role, config), package in packages.items():
        folder = output / 'linked-layout' / f'{role}-{config}'
        folder.mkdir(parents=True)
        for binary in (package / 'maelys_datalog').glob('*.so'):
            (folder / (binary.name + '.readelf')).write_text(perf.command(['readelf', '-W', '-S', '-s', '-d', binary]) + '\n')
            (folder / (binary.name + '.asm')).write_text(perf.command(['objdump', '-d', '-w', binary]) + '\n')
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
                consumer = json.loads((target / 'consumer.json').read_text())
                expected = [f'total_{i:03d}' for i in INDICES]
                if consumer != dict(scopes=expected, output_sha256=original['outputs'][case]):
                    raise ValueError('different span answers or scopes')
                profiles = []
                for path in sorted(target.glob('callgrind.out*')):
                    item = scoped_counts(path)
                    if item is not None:
                        profiles.append(dict(item, file=str(path.relative_to(output))))
                if sorted(p['label'] for p in profiles) != expected:
                    raise ValueError('incomplete span scope matrix')
                report['counts'].append(dict(config=config, case=case, role=role, repeat=repeat, profiles=profiles))
                (output / 'report.json').write_text(json.dumps(report, indent=2) + '\n')
                print('counted span', config, case, role, repeat, flush=True)
    (output / 'report.md').write_text('# Prepared IDB spans: software counts\n\n20 processes, 100 complete-request regions. Raw functions and residuals retained; no Valgrind timing used.\n')


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--span-evidence', type=Path)
    parser.add_argument('--report-sha256')
    parser.add_argument('--worker', choices=workload.CASES)
    parser.add_argument('--helper', type=Path)
    parser.add_argument('--release-evidence', type=Path)
    parser.add_argument('--parent-evidence', type=Path)
    parser.add_argument('--output', type=Path)
    args = parser.parse_args()
    if args.span_evidence:
        if args.worker or not args.output or not args.report_sha256:
            parser.error('--span-evidence requires --report-sha256 and --output, without --worker')
        span_counts(args.span_evidence, args.report_sha256, args.output)
    elif args.worker:
        if not args.helper:
            parser.error('--worker requires --helper')
        print(json.dumps(worker(args.worker, args.helper)))
    else:
        if not all((args.release_evidence, args.parent_evidence, args.output)):
            parser.error('both archived reports and an output directory are required')
        run(args.release_evidence, args.parent_evidence, args.output)
