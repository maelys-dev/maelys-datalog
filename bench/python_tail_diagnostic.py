#!/usr/bin/env python3
# SPDX-License-Identifier: MPL-2.0
"""Bounded Python tail investigation, with fixed revisions and declared passes."""
import argparse
import csv
import json
import math
import os
from pathlib import Path
import platform
import shutil
import statistics
import subprocess
import sys
import tarfile

import python_perf as perf
import python_tail_workload as probe

BASE = perf.ANCHOR
HEAD = 'f975be6a8e6778d320f48becce5d9e29c0573f6b'
CONFIG = 'SMALL-Release'
CASES = ('7-integer-prepared', '7-symbol-prepared', '7-integer-solve', '93-integer-prepared')
MODES = ('plain', 'observe', 'gc-off')
ROUNDS = 12


def schedule(rounds=ROUNDS):
    return [(role, f'aa{i}') for role in ('base', 'head') for i in range(4)] + [
        (role, f'ab{i}') for i in range(rounds)
        for role in (('base', 'head') if i % 2 == 0 else ('head', 'base'))]


def describe(value):
    a = value['samples']['total']
    result = dict(median_ns=statistics.median(a), p95_ns=perf.statistic(a, 'p95'),
                  max_ns=max(a), samples=len(a), above_200us=sum(x > 200000 for x in a))
    if value['mode'] == 'plain':
        return result
    first, last = value['total_range']
    records = value['observed'][first:last]
    assert len(records) == len(a)
    for row in records:
        row['gc_overlap_ns'] = sum(max(0, min(row['start_ns'] + row['wall_ns'], event['end_ns']) -
                                      max(row['start_ns'], event['start_ns']))
                                   for event in value['collections'])
        row['wall_minus_thread_ns'] = row['wall_ns'] - row['thread_ns']
    # These are observer-internal intervals, not the outer full-workload timings.
    for name in ('wall_ns', 'thread_ns', 'wall_minus_thread_ns'):
        numbers = [r[name] for r in records]
        result[name] = dict(median=statistics.median(numbers),
                           p95=sorted(numbers)[math.ceil(.95 * len(numbers)) - 1],
                           max=max(numbers))
    result['events'] = {name: sum(r[name] for r in records)
                        for name in ('voluntary', 'involuntary', 'minor_faults', 'major_faults', 'gc_overlap_ns')}
    # Fixed descriptive thresholds only; no samples are removed or reclassified.
    tails = [(i, r) for i, r in enumerate(records) if r['wall_ns'] > 200000]
    result['observed_above_200us'] = [dict(sample=i, **r) for i, r in tails]
    result['gc_events'] = len(value['collections'])
    return result


def run(args):
    out = args.output.resolve()
    out.mkdir(parents=True, exist_ok=False)
    commits = dict(base=BASE, head=HEAD)
    builds, hashes = {}, {}
    for role, commit in commits.items():
        source = out / role / 'source'
        source.mkdir(parents=True)
        archive = out / f'{role}.tar'
        with archive.open('wb') as stream:
            subprocess.run(['git', 'archive', commit], cwd=perf.ROOT, stdout=stream, check=True)
        with tarfile.open(archive) as stream:
            stream.extractall(source, filter='data')
        archive.unlink()
        dest = out / role / CONFIG
        dest.mkdir()
        with (dest / 'build.log').open('w') as log:
            builds[role], hashes[role] = perf.build(source, dest, CONFIG, args.compiler, log)
        print('built', role, commit, flush=True)
    # Both builds finish before any timing. Every process and sample is retained.
    raw = out / 'raw'
    raw.mkdir()
    control = probe.selfcheck()
    (out / 'observer-selfcheck.json').write_text(json.dumps(control, indent=2) + '\n')
    modes, cases = MODES, CASES[:1] if args.smoke else CASES
    rounds, count = (2, 31) if args.smoke else (ROUNDS, 501)
    plan = schedule(rounds)
    report = dict(schema=1, release_eligible=False, commits=commits, config=CONFIG,
                  observer_selfcheck=control,
                  modes=modes, cases=cases, rounds=rounds, samples=count,
                  plan=plan, binaries=hashes, results=[],
                  harness={f: perf.sha256(perf.ROOT / f) for f in
                           ('bench/python_tail_diagnostic.py', 'bench/python_tail_workload.py',
                            *perf.HARNESS_FILES)},
                  environment=dict(python=sys.version, platform=platform.platform(),
                                   compiler=perf.command([args.compiler, '--version']),
                                   run_id=os.getenv('GITHUB_RUN_ID'),
                                   harness_commit=perf.revision('HEAD')))
    expected = {}
    for index, (role, name) in enumerate(plan):
        print('measure', role, name, flush=True)
        # Rotate case/mode order to avoid always placing the suspect case first.
        ordered_cases = cases[index % len(cases):] + cases[:index % len(cases)]
        ordered_modes = modes[index % len(modes):] + modes[:index % len(modes)]
        for case in ordered_cases:
            for mode in ordered_modes:
                command = [sys.executable, '-B', perf.ROOT / 'bench/python_workload.py', case,
                           '--samples', count, '--warmup', 50] if mode == 'plain' else [
                               sys.executable, '-B', perf.ROOT / 'bench/python_tail_workload.py', case,
                               '--mode', mode, '--samples', count, '--warmup', 50]
                value = json.loads(perf.command(command, cwd=out,
                    env=dict(os.environ, PYTHONPATH=str(builds[role]), PYTHONHASHSEED='0',
                             PYTHONDONTWRITEBYTECODE='1')))
                if mode == 'plain':
                    value.update(mode='plain', release_eligible=False)
                if value['case'] != case or value['mode'] != mode:
                    raise ValueError('mismatched diagnostic output')
                if value['output_sha256'] != expected.setdefault(case, value['output_sha256']):
                    raise ValueError('query output mismatch')
                if any(len(x) != count for x in value['samples'].values()):
                    raise ValueError('incomplete sample set')
                filename = f'{role}-{name}-{case}-{mode}.json'
                (raw / filename).write_text(json.dumps(value) + '\n')
                report['results'].append(dict(role=role, pass_name=name, case=case, mode=mode,
                                              file=filename, summary=describe(value)))
    report['outputs'] = expected
    (out / 'report.json').write_text(json.dumps(report, indent=2) + '\n')
    with (out / 'summary.csv').open('w') as stream:
        writer = csv.writer(stream)
        writer.writerow(('role', 'pass', 'case', 'mode', 'median_ns', 'p95_ns', 'max_ns', 'above_200us'))
        for row in report['results']:
            writer.writerow([row[k] for k in ('role', 'pass_name', 'case', 'mode')] +
                            [row['summary'][k] for k in ('median_ns', 'p95_ns', 'max_ns', 'above_200us')])
    (out / 'report.md').write_text(
        '# Bounded Python tail diagnostic\n\n'
        'Separate diagnostic; no release approval or changed benchmark classification.\n\n'
        f'Base: `{BASE}`; candidate: `{HEAD}`; {CONFIG}.\n\n'
        f'{len(report["results"])} complete process records; inspect report.json, summary.csv '
        'and every raw sample. Observed intervals include instrumentation effects; '
        'only plain mode uses the unchanged release workload.\n')
    for role in commits:
        shutil.rmtree(out / role / 'source')
        shutil.rmtree(out / role / CONFIG / 'build')
    print('diagnostic complete', perf.sha256(out / 'report.json'), flush=True)


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--compiler', default='clang-18')
    parser.add_argument('--smoke', action='store_true')
    run(parser.parse_args())
