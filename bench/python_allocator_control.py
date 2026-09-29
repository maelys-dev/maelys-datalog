#!/usr/bin/env python3
# SPDX-License-Identifier: MPL-2.0
"""Bounded glibc intervention on the original #142 Python binaries.

No rebuild, release decision, changed workload, or correction of old timings.
The two-variable treatment changes allocator policy; it is not a trim-only proof.
"""
import argparse
from collections import Counter
import importlib.metadata
import json
import os
from pathlib import Path
import re
import shutil
import statistics
import subprocess
import sys

import python_perf as perf
import python_workload as workload

ORIGINAL_RUN = 36504178509
REPORT_SHA = '1fa3e1ceae271b226cb695d5bb1421688ff3d552dce6df69340714ea2480d20b'
COMMITS = dict(base='43bbde637435e5f6fb4756fb22c25f18f8f30583',
               head='f2372725bd077cd9211dc2f9f2a4eb77334061a1')
CONFIGS = ('SMALL-default', 'SMALL-Release')
CASE = '7-integer-solve'
TREATMENT = dict(MALLOC_TRIM_THRESHOLD_='268435456', MALLOC_TOP_PAD_='67108864')
VARIANTS = ('base_default', 'head_default', 'base_fixed', 'head_fixed')
# Four rounds: every condition occupies each position once, with reversals.
ORDERS = (VARIANTS, tuple(reversed(VARIANTS)),
          ('head_default', 'head_fixed', 'base_default', 'base_fixed'),
          ('base_fixed', 'base_default', 'head_fixed', 'head_default'))


def schedule():
    return [(v, f'aa{i}') for v in VARIANTS for i in range(4)] + [
        (v, f'ab{i}') for i, order in enumerate(ORDERS) for v in order]


def original_report(root):
    if perf.sha256(root / 'report.json') != REPORT_SHA:
        raise ValueError('wrong original report')
    report = json.loads((root / 'report.json').read_text())
    if any(report['commits'][role] != sha for role, sha in COMMITS.items()):
        raise ValueError('wrong original revisions')
    if report['schema'] != 4 or not report['positive_control']['detected']:
        raise ValueError('incomplete original protocol')
    if report['harness'] != {name: perf.sha256(perf.ROOT / name) for name in perf.HARNESS_FILES}:
        raise ValueError('changed original request harness')
    for sha in COMMITS.values():
        for config in CONFIGS:
            for name, digest in report['binaries'][f'{sha}/{config}'].items():
                if perf.sha256(root / sha / config / name) != digest:
                    raise ValueError('changed original binary/header/binding: ' + name)
    return report


def environment(parent, package, variant):
    contaminated = [k for k, v in parent.items() if v and
                    (k.startswith('MALLOC_') or k in ('GLIBC_TUNABLES', 'LD_PRELOAD'))]
    if contaminated:
        raise ValueError('inherited allocator intervention: ' + ', '.join(sorted(contaminated)))
    env = dict(parent, PYTHONPATH=str(package), PYTHONHASHSEED='0', PYTHONDONTWRITEBYTECODE='1')
    if variant.endswith('_fixed'):
        env.update(TREATMENT)
    elif not variant.endswith('_default'):
        raise ValueError('unknown allocator condition')
    return env


def summarize(value):
    rows = [x for x in value['telemetry']['requests'] if x['loop'] == 'total']
    faults = [x['minor_faults'] for x in rows]
    return dict(warm_requests=len(rows), median_ns=statistics.median(value['samples']['total']),
                p95_ns=perf.statistic(value['samples']['total'], 'p95'),
                thread_cpu_median_ns=statistics.median(x['thread_end_ns'] - x['thread_start_ns'] for x in rows),
                minor_faults=sum(faults), minor_faults_per_request=statistics.mean(faults),
                minor_fault_distribution=dict(sorted(Counter(faults).items())),
                major_faults=sum(x['major_faults'] for x in rows),
                voluntary=sum(x['voluntary'] for x in rows),
                involuntary=sum(x['involuntary'] for x in rows),
                boundary_cpu_changes=sum(x['cpu_before'] != x['cpu_after'] for x in rows))


def comparisons(samples):
    rows = []
    for base, head in (('base_default', 'head_default'), ('base_fixed', 'head_fixed'),
                       ('base_default', 'base_fixed'), ('head_default', 'head_fixed')):
        for metric in ('min', 'median', 'p95'):
            floor = max(abs(perf.statistic(samples[v][f'aa{i+1}'], metric) /
                            perf.statistic(samples[v][f'aa{i}'], metric) - 1)
                        for v in (base, head) for i in (0, 2))
            delta = [perf.statistic(samples[head][f'ab{i}'], metric) /
                     perf.statistic(samples[base][f'ab{i}'], metric) - 1 for i in range(4)]
            rows.append(dict(base=base, head=head, metric=metric, aa_floor=floor, delta=delta,
                             classification=['slower' if d > floor else 'faster' if d < -floor
                                             else 'indeterminate' for d in delta]))
    return rows


def trace_worker(count):
    """Only the separate strace process has these markers; its times are unused."""
    original = workload.repeat_transaction
    calls = scopes = 0
    def wrap(transaction, repetitions):
        if repetitions != 1:
            raise ValueError('unexpected injected work')
        def request(phases=False):
            nonlocal calls, scopes
            scoped = not phases and 51 <= calls < 51 + count
            calls += 1
            if scoped:
                os.write(2, b'ALLOC_SCOPE_BEGIN\n')
            answer = transaction(phases)
            if scoped:
                os.write(2, b'ALLOC_SCOPE_END\n')
                scopes += 1
            return answer
        return request
    workload.repeat_transaction = wrap
    try:
        value = workload.measure(CASE, count, 50, telemetry=True)
    finally:
        workload.repeat_transaction = original
    if scopes != count:
        raise ValueError('incomplete trace scopes')
    return value


def parse_trace(text, expected):
    scopes, active, brk = [], None, None
    for line in text.splitlines():
        if '"ALLOC_SCOPE_BEGIN\\n"' in line:
            if active is not None:
                raise ValueError('nested trace scope')
            active = Counter()
        elif '"ALLOC_SCOPE_END\\n"' in line:
            if active is None:
                raise ValueError('unmatched trace scope')
            scopes.append(dict(active)); active = None
        elif match := re.search(r'brk\([^)]*\)\s+= (0x[0-9a-f]+)', line):
            current = int(match[1], 16)
            if active is not None:
                active['brk_calls'] += 1
                if brk is not None and current != brk:
                    kind = 'grow' if current > brk else 'shrink'
                    active[f'brk_{kind}_calls'] += 1
                    active[f'brk_{kind}_bytes'] += abs(current - brk)
            brk = current
        elif active is not None:
            for name in ('mmap', 'munmap', 'madvise'):
                if re.search(r'\b' + name + r'\(', line):
                    active[name + '_calls'] += 1
    if active is not None or len(scopes) != expected:
        raise ValueError('incomplete trace')
    totals = Counter()
    for scope in scopes:
        totals.update(scope)
    return dict(scopes=scopes, totals=dict(totals), scope='warm total transactions only',
                limitation='Separate instrumented process; timings unused, allocator regime may differ')


def run(evidence, output, smoke=False):
    source = original_report(evidence)
    output.mkdir(parents=True, exist_ok=False)
    (output / 'raw').mkdir(); (output / 'traces').mkdir()
    shutil.copy2(evidence / 'report.json', output / 'original-report.json')
    hashes = {}
    # Copy and verify the original installed consumers before any measurement.
    for sha in COMMITS.values():
        for config in CONFIGS:
            for name, digest in source['binaries'][f'{sha}/{config}'].items():
                rel = Path('binaries') / sha / config / name
                dest = output / rel; dest.parent.mkdir(parents=True, exist_ok=True)
                shutil.copy2(evidence / sha / config / name, dest)
                if perf.sha256(dest) != digest:
                    raise ValueError('copied binary changed')
                hashes[str(rel)] = digest
    if sys.version.split()[0] != source['environment']['python'].split()[0]:
        raise ValueError('different Python interpreter version')
    packages = {k: importlib.metadata.version(k) for k in source['environment']['packages']}
    if packages != source['environment']['packages']:
        raise ValueError('different Python dependency versions')
    count = 3 if smoke else 501
    report = dict(schema=1, release_eligible=False, original_run=ORIGINAL_RUN,
                  source_report_sha256=REPORT_SHA, commits=COMMITS, configs=CONFIGS,
                  case=CASE, treatment=TREATMENT, schedule=schedule(), samples=count,
                  smoke=smoke, binaries=hashes, processes=[], comparisons=[], traces=[],
                  harness={name: perf.sha256(perf.ROOT / name) for name in
                           (*perf.HARNESS_FILES, 'bench/python_allocator_control.py')},
                  environment=dict(host=perf.host_description(), python=sys.version, packages=packages,
                                   libc=perf.command(['ldd', '--version']),
                                   strace=perf.command(['strace', '--version']),
                                   run_id=os.environ.get('GITHUB_RUN_ID'),
                                   run_attempt=os.environ.get('GITHUB_RUN_ATTEMPT')),
                  limitation='Allocator-policy intervention, not trim-only attribution or release approval; original classifications unchanged')
    for config in CONFIGS:
        samples = {v: {} for v in VARIANTS}
        for variant, pas in schedule():
            role = variant.split('_')[0]
            package = output / 'binaries' / COMMITS[role] / config / 'python'
            env = environment(os.environ, package, variant)
            command = [sys.executable, '-B', str(perf.ROOT / 'bench/python_workload.py'), CASE,
                       '--samples', str(count), '--warmup', '50', '--telemetry']
            value = json.loads(perf.command(command, cwd=output, env=env))
            perf.validate_telemetry(value, count, 50)
            if value['request_repetitions'] != 1 or value['output_sha256'] != source['outputs'][CASE]:
                raise ValueError('changed request/output')
            name = f'{config}-{variant}-{pas}.json'
            (output / 'raw' / name).write_text(json.dumps(value) + '\n')
            samples[variant][pas] = value['samples']['total']
            report['processes'].append(dict(config=config, variant=variant, pass_name=pas,
                                             raw=name, command=command, summary=summarize(value)))
            print('measured', config, variant, pas, flush=True)
        report['comparisons'].extend(dict(row, config=config) for row in comparisons(samples))
        (output / 'report.json').write_text(json.dumps(report, indent=2) + '\n')
    # All ordinary timing is finished before tracing. Trace latencies are unused.
    for config in CONFIGS:
        for variant in VARIANTS:
            role = variant.split('_')[0]
            env = environment(os.environ, output / 'binaries' / COMMITS[role] / config / 'python', variant)
            name = f'{config}-{variant}'
            trace = output / 'traces' / (name + '.strace')
            command = ['strace', '-qq', '-s', '80', '-e', 'trace=brk,mmap,munmap,madvise,write',
                       '-o', str(trace), sys.executable, '-B', str(Path(__file__).resolve()),
                       '--trace-worker', str(count)]
            with (output / 'traces' / (name + '.stderr')).open('w') as err:
                value = json.loads(subprocess.check_output(command, cwd=output, env=env, text=True, stderr=err))
            perf.validate_telemetry(value, count, 50)
            if value['output_sha256'] != source['outputs'][CASE]:
                raise ValueError('trace output mismatch')
            (output / 'traces' / (name + '.json')).write_text(json.dumps(value) + '\n')
            report['traces'].append(dict(config=config, variant=variant,
                                         **parse_trace(trace.read_text(), count)))
    (output / 'report.json').write_text(json.dumps(report, indent=2) + '\n')
    text = ['# #142 allocator-policy diagnostic', '',
            f'Original run: {ORIGINAL_RUN}; report SHA-256: `{REPORT_SHA}`.',
            'Original binaries, unchanged request, fresh processes; no build or tracing during timing.',
            'Two A/A pairs per condition, then four counterbalanced rounds; no dropped observations.',
            f'Treatment: `{TREATMENT}`. No allocator defaults or acceptance budgets are changed.',
            'Separate syscall traces have no timing interpretation. This report does not approve release.', '',
            '| Config | Variant | Pass | Median µs | p95 µs | Minor faults / request | Minor faults |',
            '|---|---|---|---:|---:|---:|---:|']
    for row in report['processes']:
        s = row['summary']
        text.append(f"| {row['config']} | {row['variant']} | {row['pass_name']} | "
                    f"{s['median_ns']/1000:.3f} | {s['p95_ns']/1000:.3f} | "
                    f"{s['minor_faults_per_request']:.3f} | {s['minor_faults']} |")
    text += ['', 'All min/median/p95 A/A floors, four round deltas/classes and scoped syscalls are in `report.json`.']
    (output / 'report.md').write_text('\n'.join(text) + '\n')


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--evidence', type=Path)
    parser.add_argument('--output', type=Path)
    parser.add_argument('--smoke', action='store_true')
    parser.add_argument('--trace-worker', type=int)
    args = parser.parse_args()
    if args.trace_worker is not None:
        print(json.dumps(trace_worker(args.trace_worker)))
    elif args.evidence is None or args.output is None:
        parser.error('--evidence and --output are required')
    else:
        run(args.evidence.resolve(), args.output.resolve(), args.smoke)
