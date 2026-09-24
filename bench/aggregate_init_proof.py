#!/usr/bin/env python3
# SPDX-License-Identifier: MPL-2.0
"""Scoped success-path costs of initializing aggregate diagnostic scratch."""
import collections
import csv
import gzip
import hashlib
import json
import platform
import re
import statistics
import subprocess
import sys
from pathlib import Path
from callgrind_exclusive import EVENTS, exclusive_dump
from compare_runs import compare, display
TARGET = 'solve_aggregate_literal'
KEY = ('op', 'order', 'size')
PROFILES = {'SMALL': 64, 'LARGE': 256}


def cases(profile):
    return {(op, order, str(size)) for op in ('count', 'min', 'max', 'sum')
            for order in ('sorted', 'reverse', 'permuted', 'duplicate', 'strided')
            for size in (0, 1, 8, 32, PROFILES[profile])}


def parse(text):
    costs = exclusive_dump(text)
    names = re.search(r'^events: (.+)$', text, re.M)[1].split()
    values = list(map(int, re.search(r'^summary: (.+)$', text, re.M)[1].split()))
    values += [0] * (len(names) - len(values))
    total = dict(zip(names, values))
    remainder = {e: total[e] - sum(v[e] for v in costs.values()) for e in EVENTS}
    if any(v < 0 for v in remainder.values()):
        raise ValueError('negative unattributed cost')
    costs['<unattributed>'] = remainder
    functions = {}
    current = None
    calls = 0
    for line in text.splitlines():
        if line.startswith(('fn=', 'cfn=')):
            value = line.split('=', 1)[1]
            match = re.fullmatch(r'\((\d+)\)(?: (.*))?', value)
            identity = match[1] if match else 'name:' + value
            if not match or match[2] is not None:
                functions[identity] = match[2] if match else value
            if line.startswith('cfn='):
                current = identity
        elif line.startswith('calls=') and current is not None:
            if re.sub(r"'\d+$", '', functions[current]) == TARGET:
                calls += int(line.split('=', 1)[1].split()[0])
    label = re.findall(r'^desc: Trigger: Client Request: (.+)$', text, re.M)
    if len(label) != 1:
        raise ValueError('expected a unique case label')
    return tuple(label[0].split('/')), costs, calls


def difference(a, b, calls):
    zero = dict.fromkeys(EVENTS, 0)
    deltas = {n: {e: b.get(n, zero)[e] - a.get(n, zero)[e] for e in EVENTS}
              for n in a.keys() | b.keys()}
    expected = {'Ir': 2 * calls, 'Dr': 0, 'Dw': calls}
    if calls <= 0 or deltas.get(TARGET) != expected:
        raise ValueError(f'aggregate delta differs from x86 zero-vector/store: {deltas.get(TARGET)}, calls={calls}')
    unexpected = {n: v for n, v in deltas.items() if n != TARGET and any(v.values())}
    if unexpected:
        raise ValueError(f'other functions changed: {unexpected}')
    return deltas[TARGET]


def load_rows(path, profile):
    rows = list(csv.DictReader(path.open()))
    result = {tuple(r[k] for k in KEY): r for r in rows}
    if len(rows) != 100 or result.keys() != cases(profile):
        raise ValueError(f'bad case inventory: {path}')
    return result


def count(root, workspace):
    if platform.machine() != 'x86_64':
        raise ValueError('acceptance target is native Linux x86_64 / Clang 18')
    out = root / 'aggregate-proof'
    out.mkdir()
    driver = Path(__file__).resolve().parent
    protocol = dict(base_head=json.loads((root / 'metadata.json').read_text()),
                    target=TARGET, expected_per_call=dict(Ir=2, Dr=0, Dw=1),
                    target_architecture=platform.machine(), profiles=PROFILES,
                    cases_per_profile=100, warmups=50, warmups_dumped_separately=True, counted_samples=1, repeats=2,
                    harness_sha256={name: hashlib.sha256((driver / name).read_bytes()).hexdigest()
                                    for name in ('bench_aggregate_init.c', 'aggregate_init_proof.py',
                                                 'callgrind_exclusive.py', 'Makefile.compare', 'compare_revisions.sh')})
    (out / 'protocol.json').write_text(json.dumps(protocol, indent=2) + '\n')
    data, table, hashes = {}, [], {}
    for profile in PROFILES:
        for role in 'AB':
            bindir = workspace / f'bin-{role}-{profile}'
            for binary_name in ('aggregates', 'aggregates-counts'):
                binary = bindir / binary_name
                hashes[f'{profile}-{role}-{binary_name}'] = hashlib.sha256(binary.read_bytes()).hexdigest()
                with gzip.open(out / f'{profile}-{role}-{binary_name}.asm.gz', 'wb') as f:
                    f.write(subprocess.check_output(['objdump', '-d', str(binary)]))
                (out / f'{profile}-{role}-{binary_name}.nm').write_bytes(subprocess.check_output(['nm', '-n', '-S', str(binary)]))
            for repeat in (1, 2):
                prefix = out / f'{profile}-{role}-{repeat}'
                with prefix.with_suffix('.log').open('w') as log:
                    subprocess.run(['valgrind', '--tool=callgrind', '--collect-atstart=no', '--cache-sim=yes', '--branch-sim=yes',
                                    '--error-exitcode=3', f'--callgrind-out-file={prefix}.out', str(bindir / 'aggregates-counts'),
                                    f'{prefix}.csv', f'{prefix}.samples.csv'], stdout=log, stderr=subprocess.STDOUT, check=True)
                rows = load_rows(prefix.with_suffix('.csv'), profile)
                assert all(all(r[k] == '0.000000' for k in ('min_us', 'median_us', 'p95_us')) for r in rows.values())
                dumps = list(out.glob(prefix.name + '.out.*'))
                assert len(dumps) == 200
                seen = set()
                warmups = set()
                for path in dumps:
                    key, costs, calls = parse(path.read_text())
                    if key[0] == 'warmup':
                        key = key[1:]
                        assert key in cases(profile) and key not in warmups
                        assert calls == 50
                        warmups.add(key)
                        continue
                    assert key in cases(profile) and key not in seen
                    seen.add(key)
                    data[profile, role, repeat, key] = (costs, calls)
                assert warmups == seen == cases(profile)
                print('counted', profile, role, repeat, '100 cases', flush=True)
    (out / 'binaries.sha256.json').write_text(json.dumps(hashes, indent=2) + '\n')
    for profile in PROFILES:
        for key in sorted(cases(profile)):
            a, acalls = data[profile, 'A', 1, key]
            b, bcalls = data[profile, 'B', 1, key]
            assert (a, acalls) == data[profile, 'A', 2, key]
            assert (b, bcalls) == data[profile, 'B', 2, key]
            # Each fixture has one aggregate literal, evaluated once per solve.
            assert acalls == bcalls == 1
            delta = difference(a, b, acalls)
            table.append(dict(profile=profile, case=key, calls=acalls, a=a[TARGET], b=b[TARGET], delta=delta, repeats_identical=True))
    (out / 'counts.json').write_text(json.dumps(table, indent=2) + '\n')
    lines = ['# Aggregate initializer: scoped exclusive counts', '',
             'Linux x86_64, Clang -O2 -g -UNDEBUG, SMALL/LARGE. 100 cases per profile: count/min/max/sum, five orders, sizes 0/1/8/32/capacity. Count one successful solve_edb after 50 separately dumped warmups; fixture preparation, checks, clocks and release are excluded. Two processes per revision/profile. All output values are checked.', '',
             'Expected x86 change per evaluated aggregate literal: +2 Ir (zero register and store), +0 Dr, +1 Dw (16-byte store). No other exclusive Ir/Dr/Dw change is permitted; all repetitions must be identical. Cache/branch model events remain in raw files and do not relax this gate.', '',
             '| Profile | Case | Calls | A Ir/Dr/Dw | B Ir/Dr/Dw | Delta | Repeats identical |',
             '|---|---|---:|---|---|---|---|']
    for r in table:
        show = lambda name: '/'.join(str(r[name][e]) for e in EVENTS)
        lines.append(f"| {r['profile']} | {' / '.join(r['case'])} | {r['calls']} | {show('a')} | {show('b')} | {show('delta')} | True |")
    lines += ['', '200/200 cases pass. No other function changes its strict exclusive counts. Raw profiles, symbols, disassembly and binary hashes are preserved. The initialization is executed on success as well as on failure; this is not a zero-cost claim.']
    (out / 'counts.md').write_text('\n'.join(lines) + '\n')
    print('200/200 aggregate cases pass', flush=True)


def report(root):
    passes = ['aa-1', 'aa-2', 'aa-3', 'aa-4', 'ab-A1', 'ab-B1', 'ab-A2', 'ab-B2']
    metadata = json.loads((root / 'metadata.json').read_text())
    lines = ['# Aggregate initializer: native timings', '', f"Base `{metadata['base']}`; head `{metadata['head']}`.", '',
             'Two A/A pairs per profile before A B A B. Same compiled engine objects as the general solver/input/session probes; all builds finish before any timing. 50 warmups, 301 samples per case/pass, solve_edb only, checked outputs. All 100 cases per profile retained. Minima below 10 microseconds; median/p95 otherwise, each with its own A/A floor. Same-binary repeatability does not bound placement between binaries; classifications do not establish a cause or universal absence of overhead. No affinity or priority changes.', '',
             '| Profile | Case | Metric | A us | B us | B/A | Floor | Verdict |', '|---|---|---|---:|---:|---:|---:|---|']
    all_rows = []
    for profile in PROFILES:
        runs = {p: load_rows(root / f'{profile}-aggregates-{p}.csv', profile) for p in passes}
        for key in sorted(cases(profile)):
            assert len({(runs[p][key]['derived'], runs[p][key]['expected']) for p in passes}) == 1
            aa = [runs[f'aa-{i}'][key] for i in range(1, 5)]
            a = [runs[f'ab-A{i}'][key] for i in (1, 2)]
            b = [runs[f'ab-B{i}'][key] for i in (1, 2)]
            metrics = ['min_us'] if statistics.median(float(x['median_us']) for x in aa) < 10 else ['median_us', 'p95_us']
            for metric in metrics:
                av,bv,ratio,floor,verdict=compare(aa,a,b,metric)
                all_rows.append(dict(profile=profile,case=key,metric=metric,a=av,b=bv,ratio=ratio,floor=floor,verdict=verdict))
                lines.append(f"| {profile} | {' / '.join(key)} | {metric} | {av:.6f} | {bv:.6f} | {display(ratio)} | {display(floor, True)} | {verdict} |")
    lines += ['', str(dict(collections.Counter(r['verdict'] for r in all_rows)))]
    (root / 'aggregates.md').write_text('\n'.join(lines)+'\n')
    (root / 'aggregates.json').write_text(json.dumps(all_rows,indent=2)+'\n')

if __name__ == '__main__':
    if sys.argv[1] == 'counts': count(Path(sys.argv[2]), Path(sys.argv[3]))
    elif sys.argv[1] == 'report': report(Path(sys.argv[2]))
    else: raise SystemExit('counts or report required')
