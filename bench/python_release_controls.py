#!/usr/bin/env python3
# SPDX-License-Identifier: MPL-2.0
"""Counterbalanced same-binary control for the fixed v0.13 observations.

Original binaries, fresh independent processes, no rebuild or affinity changes.
The base_copy role is exactly base's path and bytes, with independent samples.
"""
import argparse
import json
import os
from pathlib import Path
import statistics
import subprocess
import sys

import python_perf as perf
from python_release_instructions import original_report, REPORT_SHA

FIXTURES = (('SMALL-default', '7-integer-prepared'),
            ('LARGE-default', '7-symbol-prepared'),
            ('LARGE-Release', '7-symbol-prepared'),
            ('LARGE-Release', '93-integer-prepared'))
ROLES = ('base', 'base_copy', 'head')
# All six permutations twice; the three within-round positions are balanced.
ORDERS = (('base', 'base_copy', 'head'), ('head', 'base_copy', 'base'),
          ('base_copy', 'head', 'base'), ('base', 'head', 'base_copy'),
          ('head', 'base', 'base_copy'), ('base_copy', 'base', 'head')) * 2


def schedule():
    return [(role, f'aa{i}') for role in ROLES for i in range(4)] + [
        (role, f'ab{i}') for i, roles in enumerate(ORDERS) for role in roles]


def comparisons(samples):
    rows = []
    for variant in ('base_copy', 'head'):
        for scenario in sorted(samples['base']['aa0']):
            reference = [x for i in range(4) for x in samples['base'][f'aa{i}'][scenario]]
            metrics = ('min',) if perf.statistic(reference, 'median') < 10000 else ('median', 'p95')
            for metric in metrics:
                floor = max(abs(perf.statistic(samples[role][f'aa{i + 1}'][scenario], metric) /
                                perf.statistic(samples[role][f'aa{i}'][scenario], metric) - 1)
                            for role in ('base', variant) for i in (0, 2))
                deltas = [perf.statistic(samples[variant][f'ab{i}'][scenario], metric) /
                          perf.statistic(samples['base'][f'ab{i}'][scenario], metric) - 1
                          for i in range(len(ORDERS))]
                rows.append(dict(variant=variant, scenario=scenario, metric=metric,
                                 aa_floor=floor, delta=deltas,
                                 classification=['slower' if d > floor else 'faster' if d < -floor
                                                 else 'indeterminate' for d in deltas],
                                 median_process_delta=statistics.median(deltas)))
    return rows


def run(evidence, output, smoke=False):
    evidence, output = evidence.resolve(), output.resolve()
    source = original_report(evidence)
    output.mkdir(parents=True, exist_ok=False)
    raw = output / 'raw'
    raw.mkdir()
    commits = {role: source['commits']['head' if role == 'head' else 'base'] for role in ROLES}
    samples = {role: {name: {} for r, name in schedule() if r == role} for role in ROLES}
    report = dict(schema=1, release_eligible=False, original_run=36331650860,
                  source_report_sha256=REPORT_SHA, commits=commits, fixtures=FIXTURES,
                  schedule=schedule(), smoke=smoke, environment=perf.host_description(),
                  harness_sha256=perf.sha256(Path(__file__)), samples=samples,
                  interpretation='Independent same-binary control; never replaces the original report')
    # No preparation/build runs after the first measurement starts.
    for config, case in FIXTURES:
        for role, pas in schedule():
            env = dict(os.environ, PYTHONPATH=str(evidence / commits[role] / config / 'python'),
                       PYTHONHASHSEED='0', PYTHONDONTWRITEBYTECODE='1')
            cold = []
            repeats = (2 if smoke else 31) if case == '7-symbol-prepared' else 1
            for repeat in range(repeats):
                count = (3 if smoke else 501) if repeat == 0 else 0
                value = json.loads(perf.command([sys.executable, '-B', perf.ROOT / 'bench/python_workload.py',
                                                case, '--samples', count, '--warmup', 50, '--telemetry'],
                                               cwd=output, env=env))
                perf.validate_telemetry(value, count, 50)
                if value['request_repetitions'] != 1 or value['output_sha256'] != source['outputs'][case]:
                    raise ValueError('control output mismatch')
                (raw / f'{config}-{case}-{role}-{pas}-{repeat}.json').write_text(json.dumps(value) + '\n')
                cold.append(value['cold'])
                if repeat == 0:
                    for phase in perf.PHASES:
                        samples[role][pas][f'{config}/{case}/{phase}'] = value['samples'][phase]
            if case == '7-symbol-prepared':
                samples[role][pas][f'{config}/{case}/cold'] = cold
            print('measured', config, case, role, pas, flush=True)
        (output / 'report.json').write_text(json.dumps(report, indent=2) + '\n')
    report['rows'] = comparisons(samples)
    (output / 'report.json').write_text(json.dumps(report, indent=2) + '\n')
    text = ['# v0.13 same-binary/process controls', '',
            'base_copy uses precisely the base library and binding path, with independent samples.',
            'Two A/A pairs per label precede twelve counterbalanced rounds; no data are removed.',
            'These diagnostic classifications do not revise original release classifications.', '',
            '| Variant | Scenario | Metric | A/A % | Median round delta % | Slower / faster / indeterminate |',
            '|---|---|---|---:|---:|---|']
    for row in report['rows']:
        if not row['scenario'].endswith(('/cold', '/total')):
            continue
        classes=row['classification']
        text.append(f"| {row['variant']} | {row['scenario']} | {row['metric']} | "
                    f"{100*row['aa_floor']:.2f} | {100*row['median_process_delta']:+.2f} | " +
                    ' / '.join(str(classes.count(c)) for c in ('slower', 'faster', 'indeterminate')) + ' |')
    (output / 'report.md').write_text('\n'.join(text) + '\n')


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--evidence', required=True, type=Path)
    parser.add_argument('--output', required=True, type=Path)
    parser.add_argument('--smoke', action='store_true')
    args=parser.parse_args()
    run(args.evidence, args.output, args.smoke)
