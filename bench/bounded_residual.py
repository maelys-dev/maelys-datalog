#!/usr/bin/env python3
# SPDX-License-Identifier: MPL-2.0
"""One predeclared placement experiment. No retry or result-driven selection."""
import argparse
import collections
import csv
import gzip
import hashlib
import json
import math
import os
from pathlib import Path
import re
import shutil
import statistics
import subprocess
import tarfile
import time

from compare_runs import compare, metadata
from diagnose_sessions import CACHE_FLAGS, events
from session_proof import exclusive_dump, STRICT_EVENTS, TARGET, PREPARED

DRIVER = Path(__file__).resolve().parent.parent
SPEC = DRIVER / 'bench/residual-protocol.json'


def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def write_json(path, value):
    path.write_text(json.dumps(value, indent=2) + '\n')


def spec():
    p = json.loads(SPEC.read_text())
    expected = (DRIVER / 'bench/residual-protocol.sha256').read_text().split()[0]
    if sha(SPEC) != expected:
        raise ValueError('protocol digest mismatch')
    validate_schedule(p)
    return p


def validate_schedule(p):
    if p['pads_bytes'] != [0, 16, 32, 48, 64, 128]:
        raise ValueError('undeclared placements')
    schedule = p['schedule']
    aa = [r for r in schedule if r['phase'] == 'AA']
    first = [r for r in schedule if r.get('round') == 1]
    second = [r for r in schedule if r.get('round') == 2]
    key = lambda r: (r['driver'], r['role'], r['pad'])
    expected = {(d, r, pad) for d in p['drivers'] for r in p['revisions'] for pad in p['pads_bytes']}
    if (len(aa), len(first), len(second)) != (36, 54, 54):
        raise ValueError('wrong pass count')
    if [key(r) for r in second] != [key(r) for r in reversed(first)]:
        raise ValueError('second round must exactly reverse first')
    if set(map(key, first)) != expected or schedule[:36] != aa:
        raise ValueError('incomplete cells or AA not first')
    if collections.Counter(key(r) for r in aa) != collections.Counter({(d, r, 0): 4 for d in p['drivers'] for r in p['revisions']}):
        raise ValueError('two AA pairs required for every driver/revision')


def run(root, command, *, cwd=None, env=None, output=None):
    command = list(map(str, command))
    with (root / 'commands.jsonl').open('a') as f:
        f.write(json.dumps({'time': time.time(), 'cwd': str(cwd or DRIVER), 'command': command}) + '\n')
    with (output or root / 'commands.log').open('a') as f:
        subprocess.run(command, cwd=cwd or DRIVER, env=env, stdout=f, stderr=subprocess.STDOUT, check=True)


def prepare(root, base, head):
    p = spec()
    if (base, head) != (p['revisions']['A'], p['revisions']['C']):
        raise ValueError('base/head must match immutable archived protocol')
    root.mkdir(parents=True, exist_ok=False)
    pre = root / 'preregistered'
    pre.mkdir()
    for name in ('protocol.json', 'protocol.sha256', 'PROTOCOL.md'):
        shutil.copyfile(DRIVER / ('bench/residual-' + name), pre / name)
    tracked = subprocess.check_output(['git', 'ls-files', 'bench', '.github/workflows/bench-compare.yml'], cwd=DRIVER, text=True).splitlines()
    write_json(pre / 'tooling.json', {
        'commit': subprocess.check_output(['git', 'rev-parse', 'HEAD'], cwd=DRIVER, text=True).strip(),
        'dirty': bool(subprocess.check_output(['git', 'status', '--porcelain'], cwd=DRIVER, text=True).strip()),
        'sha256': {name: sha(DRIVER / name) for name in tracked},
        'general_schedule': general_schedule(),
        'timing_rule': 'Retain baseline AA classification; also show candidate AA floor and max-floor classification. No signal erased.',
        'session_layout_limit': 'Opaque session/EDB internals unobserved; public fixture bases and fact terms observed in each process.',
    })
    available = {}
    for name in ('/proc/sys/kernel/perf_event_paranoid', '/proc/cpuinfo', '/sys/bus/event_source/devices/cpu/type'):
        try:
            available[name] = Path(name).read_text()
        except OSError as e:
            available[name] = type(e).__name__
    available['hardware_counter_measurement'] = 'not measured; metadata alone does not prove access; no permission change or dedicated run'
    write_json(pre / 'hardware-availability.json', available)


def general_schedule():
    # A/C are called A/B only in historical report filenames.
    result = []
    for profile in ('SMALL', 'LARGE'):
        for repeat in range(1, 5):
            result.append((profile, 'A', f'aa-{repeat}'))
    for profile in ('SMALL', 'LARGE'):
        for repeat in (1, 2):
            result.extend([(profile, 'A', f'ab-A{repeat}'), (profile, 'C', f'ab-B{repeat}')])
    return result


def build(root):
    p = spec()
    workspace = root / 'workspace'
    workspace.mkdir()
    for role, revision in p['revisions'].items():
        source = workspace / role
        source.mkdir()
        archive = workspace / (role + '.tar')
        run(root, ['git', 'archive', '--format=tar', '-o', archive, revision])
        with tarfile.open(archive) as tar:
            tar.extractall(source, filter='data')
        archive.unlink()
        if sha(source / 'bench/bench_datalog.c') != sha(DRIVER / 'bench/bench_datalog.c'):
            raise ValueError('historical general solver harness differs')
        for profile in ('SMALL', 'LARGE'):
            print('build', role, profile, flush=True)
            run(root, ['make', '-j1', '-f', DRIVER / 'bench/Makefile.residual', 'residual',
                       f'DRIVER={DRIVER}', f'OUT={workspace}/bin-{role}-{profile}',
                       f'REVISION={revision}', f'PROFILE={profile}'], cwd=source)
    record_binaries(root)
    write_json(root / 'build-complete.json', {'time': time.time(), 'all_builds_before_measurement': True})


def binary(root, driver, role, pad):
    profile = driver.split('-')[0]
    stem = 'solver' if 'solver' in driver else 'session'
    return root / f'workspace/bin-{role}-{profile}/{stem}-residual-{pad}'


def symbol_map(path):
    raw = subprocess.check_output(['nm', '-n', '-S', str(path)], text=True)
    symbols = {}
    for line in raw.splitlines():
        m = re.fullmatch(r'([0-9a-f]+) ([0-9a-f]+) [tT] (.+)', line)
        if m:
            symbols[m[3]] = {'address': int(m[1], 16), 'size': int(m[2], 16)}
    return raw, symbols


def record_binaries(root):
    p = spec()
    out = root / 'binaries'
    out.mkdir()
    manifest = {}
    for driver in p['drivers']:
        for role in p['revisions']:
            origin = None
            for pad in p['pads_bytes']:
                src = binary(root, driver, role, pad)
                name = f'{driver}-{role}-pad{pad}'
                dst = out / name
                shutil.copy2(src, dst)
                raw, symbols = symbol_map(dst)
                (out / (name + '.nm')).write_text(raw)
                if origin is None:
                    origin = symbols
                selected = {k: v for k, v in symbols.items() if any(s in k for s in ('fact_cmp', 'derive_ordered', 'sort', 'materializ', 'solve_aggregate'))}
                for required in (TARGET, 'maelys_datalog_fact_cmp'):
                    if required not in symbols:
                        raise ValueError('missing target symbol ' + required)
                # Every engine text symbol downstream of the insertion must shift exactly.
                anchor = origin['maelys_datalog_edb_add_fact']['address']
                for key, before in origin.items():
                    after = symbols[key]
                    if after['size'] != before['size']:
                        raise ValueError('padding changed symbol size: ' + key)
                    expected = pad if before['address'] >= anchor and key.startswith(('maelys_', 'solve_', 'evaluate_', 'aggregate_')) else None
                    if expected is not None and after['address'] - before['address'] != expected:
                        raise ValueError('wrong displacement: ' + key)
                dis = subprocess.check_output(['objdump', '-d', str(dst)])
                with gzip.open(out / (name + '.asm.gz'), 'wb') as f:
                    f.write(dis)
                manifest[name] = {'sha256': sha(dst), 'symbols': selected,
                                  'displacement': {k: v['address'] - origin[k]['address'] for k, v in selected.items()}}
    for directory in (root / 'workspace').glob('bin-*'):
        for stem in ('solver', 'input', 'sessions'):
            src = directory / stem
            dst = out / f'{directory.name}-{stem}'
            shutil.copy2(src, dst)
            manifest[dst.name] = {'sha256': sha(dst)}
    write_json(out / 'manifest.json', manifest)


def general(root):
    if not (root / 'build-complete.json').exists():
        raise ValueError('build gate missing')
    p = spec()
    output = root / 'general'
    output.mkdir()
    metadata(str(output), p['revisions']['A'], p['revisions']['C'], str(DRIVER))
    for profile, role, name in general_schedule():
        print('general', profile, role, name, flush=True)
        directory = root / f'workspace/bin-{role}-{profile}'
        for kind in ('solver', 'input', 'sessions'):
            prefix = output / f'{profile}-{kind}-{name}'
            run(root, [directory / kind, str(prefix) + '.csv', str(prefix) + ('.json' if kind == 'solver' else '.samples.csv')],
                env=dict(os.environ, MAELYS_BENCH_SAMPLES='1000'))
    for script, name in [('compare_runs.py', 'comparison.md'), ('compare_sessions.py', 'sessions.md')]:
        run(root, ['python3', DRIVER / 'bench' / script, output], output=output / name)


def fixtures(p, driver):
    return [f for f in p['fixtures'] if f['profile'] == driver.split('-')[0] and
            (len(f['case']) == 2) == ('solver' in driver)]


def invocation(root, driver, role, pad, fixture, prefix, mode):
    exe = binary(root, driver, role, pad)
    if 'solver' in driver:
        return [exe, mode, str(prefix) + '.samples.csv', '2048']
    return [exe, str(prefix) + '.csv', str(prefix) + '.samples.csv', *fixture['case'], mode]


def timings(root):
    if not (root / 'build-complete.json').exists():
        raise ValueError('build gate missing')
    p = spec()
    out = root / 'timings'
    out.mkdir()
    for index, entry in enumerate(p['schedule']):
        print('placement', index + 1, '/', len(p['schedule']), entry, flush=True)
        for f in fixtures(p, entry['driver']):
            name = f'{index:03d}-' + '-'.join(f['case'])
            prefix = out / name
            run(root, invocation(root, entry['driver'], entry['role'], entry['pad'], f, prefix, 'time'),
                env=dict(os.environ, MAELYS_RESIDUAL_LAYOUT=str(prefix) + '.layout.csv'))
    write_json(out / 'complete.json', {'passes': len(p['schedule'])})


def counts(root):
    p = spec()
    out = root / 'counts'
    out.mkdir()
    records = []
    for driver in p['drivers']:
        for role in p['revisions']:
            for pad in p['pads_bytes']:
                for f in fixtures(p, driver):
                    for repeat in (1, 2):
                        name = '-'.join([driver, role, str(pad), *f['case'], str(repeat)])
                        prefix = out / name
                        raw = out / (name + '.out')
                        print('count', name, flush=True)
                        run(root, ['valgrind', '--tool=callgrind', '--collect-atstart=no', '--error-exitcode=99',
                                   *CACHE_FLAGS, f'--callgrind-out-file={raw}',
                                   *invocation(root, driver, role, pad, f, prefix, 'count')],
                            env=dict(os.environ, MAELYS_RESIDUAL_LAYOUT=str(prefix) + '.layout.csv'))
                        total = events(raw)
                        functions = exclusive_dump(raw.read_text())
                        remainder = {e: total[e] - sum(row[e] for row in functions.values()) for e in next(iter(functions.values()))}
                        records.append(dict(driver=driver, role=role, pad=pad, case=f['case'], repeat=repeat,
                                            total=total, functions=functions, remainder=remainder, raw=raw.name))
    write_json(out / 'counts.json', records)


def strict_counts(records):
    failures, rows = [], []
    groups = collections.defaultdict(dict)
    for r in records:
        groups[r['driver'], tuple(r['case'])][r['role'], r['pad'], r['repeat']] = r
    for (driver, case), values in groups.items():
        for role in 'ABC':
            origin = values[role, 0, 1]
            for pad in (0, 16, 32, 48, 64, 128):
                for repeat in (1, 2):
                    r = values[role, pad, repeat]
                    for name in origin['functions'].keys() | r['functions'].keys():
                        if any(origin['functions'].get(name, {}).get(e, 0) != r['functions'].get(name, {}).get(e, 0) for e in STRICT_EVENTS):
                            failures.append(dict(kind='repeat_or_placement_drift', driver=driver, case=case, role=role, pad=pad, repeat=repeat, function=name))
        for pad in (0, 16, 32, 48, 64, 128):
            a, b, c = (values[role, pad, 1] for role in 'ABC')
            changes = {}
            for name in a['functions'].keys() | c['functions'].keys():
                delta = {e: c['functions'].get(name, {}).get(e, 0) - a['functions'].get(name, {}).get(e, 0) for e in STRICT_EVENTS}
                if any(delta.values()):
                    changes[name] = delta
                allowed = dict(zip(STRICT_EVENTS, (8, 1, 2))) if name == PREPARED and 'session' in driver else dict.fromkeys(STRICT_EVENTS, 0)
                if any(delta[e] > allowed[e] for e in STRICT_EVENTS):
                    failures.append(dict(kind='unexpected_increase', driver=driver, case=case, pad=pad, function=name, delta=delta))
            rows.append(dict(driver=driver, case=case, pad=pad, changes=changes,
                             totals={r['role']: r['total'] for r in (a, b, c)},
                             ordered={r['role']: r['functions'].get(TARGET, {}) for r in (a, b, c)}))
    return {'failures': failures, 'cases': rows}


def read_stats(prefix, solver=False):
    with Path(str(prefix) + '.samples.csv').open() as stream:
        rows = list(csv.DictReader(stream))
    expected = 1000 if solver else 301
    if len(rows) != expected:
        raise ValueError('wrong sample count ' + str(prefix))
    samples = sorted(float(r['elapsed_us']) for r in rows)
    if any(not math.isfinite(v) or v <= 0 for v in samples):
        raise ValueError('invalid timing')
    if solver:
        if {r['result'] for r in rows} != {'17'}:
            raise ValueError('wrong solver oracle')
        # Match report_solver_layout.py: an even-sized median averages both
        # central samples; p95 uses nearest rank, ceil(.95*n)-1 (zero based).
        return dict(min_us=samples[0], median_us=statistics.median(samples),
                    p95_us=samples[math.ceil(.95 * len(samples))-1], result_digest='17')
    with Path(str(prefix) + '.csv').open() as stream:
        summary = list(csv.DictReader(stream))
    if len(summary) != 1:
        raise ValueError('unexpected selected case count')
    result = summary[0]
    for key, value in zip(('min_us', 'median_us', 'p95_us'), (samples[0], samples[len(samples)//2], samples[len(samples)*95//100])):
        if abs(float(result[key]) - value) > 1e-6:
            raise ValueError('summary/raw disagreement')
    return result


def report(root):
    p = spec()
    data = collections.defaultdict(dict)
    for index, entry in enumerate(p['schedule']):
        for f in fixtures(p, entry['driver']):
            prefix = root / 'timings' / (f'{index:03d}-' + '-'.join(f['case']))
            stats = read_stats(prefix, 'solver' in entry['driver'])
            data[entry['driver'], tuple(f['case'])][entry['phase'], entry['role'], entry['pad'], entry.get('repeat', entry.get('round'))] = stats
    lines = ['# Bounded placement diagnostic', '',
             'One run; archived protocol and all passes retained. A=base, B=initial, C=noinline. Decision A/C.',
             'Historical +6.44% SMALL and +10.38% separate LARGE residuals remain observations from their original run.',
             'Diagnostic drivers have their own layout; the complete general matrices are separate evidence.',
             'Hardware counters not measured. Callgrind cache/branch events are simulated, not hardware counters.',
             'An AA floor is repeatability, not a bound on placement. Six placements are not a universal bound.', '',
             '| Driver / case | Comparison | Pad | Metric | Ratio | AA base | AA candidate | Base-floor class | Max-floor class | Round 1 | Round 2 |',
             '|---|---|---:|---|---:|---:|---:|---|---|---:|---:|']
    table, amplitudes = [], []
    for (driver, case), values in data.items():
        if len({r['result_digest'] for r in values.values()}) != 1:
            raise ValueError('digest changed across revisions or placements')
        for old, new in [('A', 'C'), ('B', 'C')]:
            aa = [values['AA', old, 0, i] for i in range(1, 5)]
            cc = [values['AA', new, 0, i] for i in range(1, 5)]
            metrics = ('min_us',) if statistics.median(float(r['median_us']) for r in aa) < 10 else ('median_us', 'p95_us')
            for pad in p['pads_bytes']:
                a = [values['placement', old, pad, i] for i in (1, 2)]
                c = [values['placement', new, pad, i] for i in (1, 2)]
                for metric in metrics:
                    av, cv, ratio, floor, verdict = compare(aa, a, c, metric)
                    cf = compare(cc, a, c, metric)[3]
                    combined = 'indéterminé' if max(ratio, 1/ratio)-1 <= max(floor, cf) else verdict
                    rounds = [compare(aa, [a[i]]*2, [c[i]]*2, metric) for i in (0, 1)]
                    row = dict(driver=driver, case=case, comparison=old+'/'+new, pad=pad, metric=metric,
                               baseline_us=av, candidate_us=cv, ratio=ratio, base_floor=floor, candidate_floor=cf,
                               verdict=verdict, max_floor_verdict=combined,
                               round_ratios=[r[2] for r in rounds], round_verdicts=[r[4] for r in rounds])
                    table.append(row)
                    lines.append(f"| {driver}/{'/'.join(case)} | {old}/{new} | {pad} | {metric} | {ratio:.5f} | {floor:.2%} | {cf:.2%} | {verdict} | {combined} | {rounds[0][2]:.5f} ({rounds[0][4]}) | {rounds[1][2]:.5f} ({rounds[1][4]}) |")
        for role in 'ABC':
            for metric in ('min_us', 'median_us', 'p95_us'):
                for repeat in (1, 2):
                    v = [float(values['placement', role, pad, repeat][metric]) for pad in p['pads_bytes']]
                    amplitudes.append(dict(driver=driver, case=case, role=role, metric=metric, round=repeat,
                                           amplitude=max(v)/min(v)-1, values=v))
    write_json(root / 'timing-comparisons.json', table)
    write_json(root / 'placement-amplitudes.json', amplitudes)
    records = json.loads((root / 'counts/counts.json').read_text())
    expected = {(d, role, pad, tuple(f['case']), repeat) for d in p['drivers']
                for role in p['revisions'] for pad in p['pads_bytes']
                for f in fixtures(p, d) for repeat in (1, 2)}
    actual = [(r['driver'], r['role'], r['pad'], tuple(r['case']), r['repeat']) for r in records]
    if len(actual) != len(expected) or set(actual) != expected:
        raise ValueError('incomplete or duplicate scoped-count inventory')
    verdict = strict_counts(records)
    write_json(root / 'acceptance.json', verdict)
    lines += ['', '## Scoped instruction evidence', '',
              '| Driver / case | Pad | Ordered ΔIr C−A | ΔDr | ΔDw | Total Ir A / B / C |', '|---|---:|---:|---:|---:|---|']
    for row in verdict['cases']:
        delta = {e: row['ordered']['C'].get(e, 0)-row['ordered']['A'].get(e, 0) for e in STRICT_EVENTS}
        lines.append(f"| {row['driver']}/{'/'.join(row['case'])} | {row['pad']} | {delta['Ir']} | {delta['Dr']} | {delta['Dw']} | " + ' / '.join(str(row['totals'][r]['Ir']) for r in 'ABC') + ' |')
    lines += ['', f"Strict failures: {len(verdict['failures'])}. See acceptance.json for every changed function, and counts.json for all exclusive costs, totals and unassigned remainder.",
              'Simulated Bcm and I1mr are retained separately in those raw records.', '',
              'No timing-only engine attribution. If no mechanism is isolated, retain the unresolved residuals, document the limitation and stop. This does not block 0.10.0.']
    (root / 'REPORT.md').write_text('\n'.join(lines) + '\n')
    if verdict['failures']:
        raise SystemExit('Strict count criterion failed; all evidence and the report are preserved. No automatic retry.')


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('phase', choices=['prepare', 'build', 'general', 'timings', 'counts', 'report'])
    parser.add_argument('output', type=Path)
    parser.add_argument('--base')
    parser.add_argument('--head')
    args = parser.parse_args()
    if args.phase == 'prepare':
        prepare(args.output, args.base, args.head)
    else:
        globals()[args.phase](args.output)


if __name__ == '__main__':
    main()
