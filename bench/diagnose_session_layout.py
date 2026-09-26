#!/usr/bin/env python3
# SPDX-License-Identifier: MPL-2.0
"""Bounded prepared-session diagnosis; prepare ALL builds before run/timing."""
import csv
import gzip
import hashlib
import json
import math
import os
from pathlib import Path
import platform
import re
import subprocess
import sys
import tempfile

from compare_runs import compare, display
from report_solver_layout import address

# Declared before measurement: adverse symbol cases and matched integer controls.
CASES = [(profile, 'derive', order, value, size)
         for profile, order, size in (
             ('SMALL', 'permuted', '31'), ('SMALL', 'permuted', 'maximum'),
             ('LARGE', 'duplicate', '402'), ('LARGE', 'sorted', 'maximum'))
         for value in ('symbol', 'integer')]
PADS = (0, 16)
RESULT_ARRAYS = ('facts_per_pred', 'stratum_idb_end', 'edb_facts', 'idb_facts',
                 'idb_proof_index', 'edb_ranges', 'proof', 'premise_pool',
                 'node_premise_begin', 'node_premise_count', 'node_has_premises', 'witness_slots')


def restored_result_layout(base, original, revised):
    rows = []
    for member in RESULT_ARRAYS:
        key = f'offsetof.result.{member}'
        size = f'sizeof.result.{member}'
        if any(key not in d or size not in d for d in (base, original, revised)):
            raise ValueError(f'missing result layout evidence: {member}')
        if revised[key] != base[key] or revised[size] != base[size]:
            raise ValueError(f'revised result layout is not restored: {member}')
        rows.append((member, base[key], original[key], revised[key], base[size]))
    return rows

SYMBOLS = ('solve_once_derive_ordered', 'maelys_datalog_session_solve_edb',
           'maelys_datalog_prepared_session_materialize_inputs_diagnosed')
DRIVER = Path(__file__).resolve().parent.parent


def command(args, **kwargs):
    print('+', ' '.join(map(str, args)), flush=True)
    return subprocess.run(list(map(str, args)), check=True, **kwargs)


def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def prepare(base, head, root, original=''):
    if platform.system() != 'Linux':
        raise ValueError('Linux required (local containers are smoke tests only)')
    root.mkdir(exist_ok=False)
    work = Path(tempfile.mkdtemp(prefix='session-layout-'))
    refs = dict(A=base, B=head) if not original else dict(A=base, B=original, C=head)
    revisions = {role: subprocess.check_output(
        ['git', '-C', str(DRIVER), 'rev-parse', '--verify', '--end-of-options', ref + '^{commit}'],
        text=True).strip() for role, ref in refs.items()}
    manifest = dict(revisions=revisions, workspace=str(work), cases=CASES, pads=PADS,
                    host=platform.platform(), priority='unchanged', affinity='unchanged',
                    driver=subprocess.check_output(['git', '-C', str(DRIVER), 'rev-parse', 'HEAD'], text=True).strip())
    (root / 'manifest.json').write_text(json.dumps(manifest, indent=2) + '\n')
    files = [DRIVER / 'bench' / name for name in (
        'bench_sessions.c', 'types_compat.h', 'session_layout.h', 'session_layout_runtime.c',
        'session_layout_solver.c', 'Makefile.session-layout', 'Makefile.compare',
        'diagnose_session_layout.py', 'compare_runs.py', 'report_solver_layout.py')]
    (root / 'harness.sha256').write_text(''.join(f'{digest(p)}  {p.name}\n' for p in files))
    with (root / 'environment.txt').open('w') as out:
        for args in (['uname', '-a'], ['clang', '--version'], ['valgrind', '--version'], ['lscpu']):
            command(args, stdout=out)
        for p in sorted(Path('/sys/devices/system/cpu/cpu0/cache').glob('index*/coherency_line_size')):
            out.write(f'{p}: {p.read_text()}')
    binaries = {}
    for role, revision in revisions.items():
        source = work / role
        source.mkdir()
        archive = work / f'{role}.tar'
        command(['git', '-C', DRIVER, 'archive', '--output', archive, revision])
        command(['tar', '-xf', archive, '-C', source])
        for profile in ('SMALL', 'LARGE'):
            destination = work / f'bin-{role}-{profile}'
            env = dict(os.environ)
            env.pop('MAKEFLAGS', None)
            env.pop('MFLAGS', None)
            command(['make', '-j1', '-C', source, '-f', DRIVER / 'bench/Makefile.session-layout',
                     'session-layout', f'DRIVER={DRIVER}', f'OUT={destination}',
                     f'REVISION={revision}', f'PROFILE={profile}'], env=env)
            for kind in ('layout', 'counts'):
                for pad in PADS:
                    binary = destination / f'{kind}-{pad}'
                    prefix = root / f'{role}-{profile}-{kind}-{pad}'
                    binaries[str(binary)] = digest(binary)
                    with prefix.with_suffix('.nm').open('w') as out:
                        command(['nm', '-n', '-S', binary], stdout=out)
                    # Stream disassembly through a temporary file, then compress.
                    assembly = prefix.with_suffix('.asm')
                    with assembly.open('wb') as out:
                        command(['objdump', '-d', binary], stdout=out)
                    with assembly.open('rb') as inp, gzip.open(str(assembly) + '.gz', 'wb') as out:
                        for block in iter(lambda: inp.read(1024 * 1024), b''):
                            out.write(block)
                    assembly.unlink()
                for symbol in SYMBOLS:
                    files = [root / f'{role}-{profile}-{kind}-{pad}.nm' for pad in PADS]
                    if address(files[1], symbol) - address(files[0], symbol) != 16:
                        raise ValueError(f'padding displacement is not 16: {role}/{profile}/{kind}/{symbol}')
    (root / 'binaries.json').write_text(json.dumps(binaries, indent=2) + '\n')
    (root / 'prepared').write_text('All builds and displacement checks finished.\n')


def prefix(case, role, suffix):
    return '-'.join((*case, role, suffix))


def run(root):
    if not (root / 'prepared').exists() or (root / 'started').exists():
        raise ValueError('requires complete preparation and a fresh measurement')
    manifest = json.loads((root / 'manifest.json').read_text())
    for binary, expected in json.loads((root / 'binaries.json').read_text()).items():
        if digest(Path(binary)) != expected:
            raise ValueError(f'binary changed after preparation: {binary}')
    (root / 'started').write_text('Timing started; no subsequent build.\n')
    work = Path(manifest['workspace'])
    roles = list(manifest['revisions'])

    def measure(case, role, pad, suffix, count=False):
        name = prefix(case, role, suffix)
        binary = work / f'bin-{role}-{case[0]}' / f'{"counts" if count else "layout"}-{pad}'
        args = [binary, root / f'{name}.csv', root / f'{name}.samples.csv', *case[1:], root / f'{name}.layout.csv']
        if count:
            args = ['valgrind', '--tool=callgrind', '--collect-atstart=no', '--cache-sim=yes',
                    '--dump-instr=yes', '--error-exitcode=3', f'--callgrind-out-file={root}/{name}.out', *args]
        with (root / f'{name}.log').open('w') as log:
            command(args, stdout=log, stderr=log)
        if count:
            with (root / f'{name}.functions.txt').open('w') as out:
                command(['callgrind_annotate', '--auto=no', '--threshold=100', '--show=Ir,Dr,Dw',
                         '--show-percs=no', root / f'{name}.out'], stdout=out)

    # Two A/A pairs for EVERY unperturbed revision, before any comparison.
    for case in CASES:
        for role in roles:
            for repeat in range(1, 5):
                measure(case, role, 0, f'aa{repeat}')
    schedule = [(case, pad, role) for case in CASES for pad in PADS for role in roles]
    for repeat in (1, 2):
        for case, pad, role in (schedule if repeat == 1 else reversed(schedule)):
            measure(case, role, pad, f'pad{pad}-round{repeat}')
    # Software instruction/data-access counts in separate processes; never timings.
    for case, pad, role in schedule:
        for repeat in (1, 2):
            measure(case, role, pad, f'count{pad}-round{repeat}', count=True)
    report(root)


def layout(path):
    with path.open(newline='') as stream:
        rows = list(csv.DictReader(stream))
    data = {r['key']: int(r['value']) for r in rows}
    required = ('session', 'inputs', 'program', 'symbols', 'result')
    if len(data) != len(rows) or any(v < 0 for v in data.values()):
        raise ValueError(f'duplicate/invalid layout: {path}')
    for name in required:
        size, align, addr = (data.get(k + '.' + name, 0) for k in ('sizeof', 'alignof', 'address'))
        if not size or not align or align & (align - 1) or not addr or addr % align:
            raise ValueError(f'missing/invalid object layout: {path}/{name}')
    for key, value in data.items():
        if key.startswith('address.') and data.get(key.replace('address.', 'address_mod64.', 1)) != value % 64:
            raise ValueError(f'inconsistent address modulo: {path}/{key}')
        if key.startswith('offsetof.'):
            name, member = key.removeprefix('offsetof.').split('.', 1)
            if (data.get(f'address.{name}.{member}') != data[f'address.{name}'] + value or
                    value + data.get(f'sizeof.{name}.{member}', 0) > data[f'sizeof.{name}']):
                raise ValueError(f'inconsistent member layout: {path}/{key}')
    for name, member in (('symbols', 'entries'), ('symbols', 'storage'), ('symbols', 'index'),
                         ('inputs', 'fact_pool'), ('result', 'edb_facts'), ('result', 'idb_facts'),
                         ('program', 'rules'), ('program', 'facts')):
        if f'offsetof.{name}.{member}' not in data:
            raise ValueError(f'missing hot member: {path}/{name}.{member}')
    if (data.get('address.resolved_program') != data['address.program'] or
            data.get('address.resolved_symbols') != data['address.symbols']):
        raise ValueError(f'probe does not describe the live solver: {path}')
    return data


def measurement(root, case, role, suffix, revision, count=False):
    name = prefix(case, role, suffix)
    with (root / f'{name}.csv').open(newline='') as stream:
        rows = list(csv.DictReader(stream))
    if len(rows) != 1:
        raise ValueError(f'expected one case: {name}')
    row = rows[0]
    n = 1 if count else 301
    if (tuple(row[k] for k in ('profile', 'policy', 'order', 'values', 'size')) != tuple(case) or
            row['commit'] != revision or int(row['samples']) != n):
        raise ValueError(f'case/revision/sample mismatch: {name}')
    with (root / f'{name}.samples.csv').open(newline='') as stream:
        raw = list(csv.DictReader(stream))
    if (len(raw) != n or [r['sample'] for r in raw] != list(map(str, range(n))) or
            any(tuple(r[k] for k in ('policy', 'order', 'values', 'size')) != tuple(case[1:]) for r in raw)):
        raise ValueError(f'raw sample inventory: {name}')
    times = sorted(float(r['elapsed_us']) for r in raw)
    if any(not math.isfinite(t) or t < 0 or (count and t != 0) for t in times):
        raise ValueError(f'invalid timing: {name}')
    for metric, expected in zip(('min_us', 'median_us', 'p95_us'), (times[0], times[n // 2], times[n * 95 // 100])):
        if not math.isclose(float(row[metric]), expected, abs_tol=0.000001):
            raise ValueError(f'summary/raw mismatch: {name}/{metric}')
    return row, layout(root / f'{name}.layout.csv')


def counts(root, name):
    text = (root / f'{name}.out').read_text()
    events = re.findall(r'^events: (.+)$', text, re.M)
    summaries = re.findall(r'^summary: (.+)$', text, re.M)
    totals = re.findall(r'^totals: (.+)$', text, re.M)
    if len(events) != 1 or len(summaries) != 1 or len(totals) != 1:
        raise ValueError(f'Callgrind inventory: {name}')
    event_names, values = events[0].split(), list(map(int, summaries[0].split()))
    if len(values) > len(event_names) or len(values) < 3 or any(k not in event_names for k in ('Ir', 'Dr', 'Dw')):
        raise ValueError(f'Callgrind events: {name}')
    values += [0] * (len(event_names) - len(values))  # Callgrind omits trailing zeros.
    total = dict(zip(event_names, values))
    funcs = {}
    for line in (root / f'{name}.functions.txt').read_text().splitlines():
        match = re.match(r'^\s*([\d,]+|\.)\s+([\d,]+|\.)\s+([\d,]+|\.)\s+\S+:(.+?)(?:\s+\[.*\])?\s*$', line)
        if match:
            function = re.sub(r"'\d+$", '', match[4])
            cost = tuple(0 if v == '.' else int(v.replace(',', '')) for v in match.group(1, 2, 3))
            funcs[function] = tuple(a + b for a, b in zip(funcs.get(function, (0, 0, 0)), cost))
    if not funcs or total['Ir'] <= 0:
        raise ValueError(f'missing scoped counts: {name}')
    # Callgrind's summary can exceed its attributed totals at collection
    # boundaries. Preserve this residual explicitly; never silently drop it.
    attributed = list(map(int, totals[0].split()))
    if len(attributed) > len(event_names) or len(attributed) < 3:
        raise ValueError(f'Callgrind totals: {name}')
    attributed += [0] * (len(event_names) - len(attributed))
    by_event = dict(zip(event_names, attributed))
    if tuple(map(sum, zip(*funcs.values()))) != tuple(by_event[k] for k in ('Ir', 'Dr', 'Dw')):
        raise ValueError(f'incomplete per-function accounting: {name}')
    residual = tuple(total[k] - by_event[k] for k in ('Ir', 'Dr', 'Dw'))
    if any(v < 0 for v in residual):
        raise ValueError(f'negative unattributed residual: {name}')
    funcs['[unattributed summary minus totals]'] = residual
    return tuple(total[k] for k in ('Ir', 'Dr', 'Dw')), funcs


def report(root):
    manifest = json.loads((root / 'manifest.json').read_text())
    roles = list(manifest['revisions'])
    lines = ['# Prepared-session layout diagnostic', '',
             'Two text placements (0/16 bytes), eight predeclared fixtures. Two A/A pairs per unpadded revision, '
             'then two counterbalanced rounds. No affinity/priority change. All builds precede timing.', '',
             'Each layout CSV describes the actual live session/result of its timed process, after its last sample. '
             'Modulo 64 is a reference, not proof of cache-line sharing or a cache mechanism. '
             'The diagnostic translation units change code placement relative to the full matrix; interpret that matrix separately.', '',
             'Callgrind Ir/Dr/Dw are software counts in solve_edb only, excluding preparation, clocks, queries and release. '
             'Cache misses are simulated, not hardware evidence. Flat counts do not establish equal cycle or cache costs. '
             'Any gap between Callgrind summary and attributed totals is retained as an explicit unattributed row in functions.csv.', '',
             'A = baseline; B = candidate.' if len(roles) == 2 else 'A = baseline; B = original; C = revised.', '',
             'No revised data layout is tested in a two-revision run. A causal claim requires a justified revised candidate '
             'in the same three-revision run; these observations alone do not select a new layout.', '',
             '| Case | Revision/base | Pad | Metric | A | Candidate | Change | Base A/A floor | Candidate A/A floor | Classification |',
             '| --- | --- | ---: | --- | ---: | ---: | ---: | ---: | ---: | --- |']
    layout_rows, function_rows, count_rows, count_lines, restored = [], [], [], [], {}
    for case in CASES:
        identity, structural = None, {}
        def read(role, suffix, count=False):
            nonlocal identity
            row, data = measurement(root, case, role, suffix, manifest['revisions'][role], count)
            invariant = tuple(row[k] for k in ('entries', 'edb_limit', 'result_digest', 'compiler', 'cflags', 'opt_level'))
            if identity is None:
                identity = invariant
            if invariant != identity:
                raise ValueError(f'output/build mismatch: {case}/{role}/{suffix}')
            shape = {k: v for k, v in data.items() if not k.startswith('address')}
            if role in structural and structural[role] != shape:
                raise ValueError(f'data layout changed between placements: {case}/{role}')
            structural[role] = shape
            layout_rows.extend((prefix(case, role, suffix), k, v) for k, v in data.items())
            return row
        aa = {role: [read(role, f'aa{i}') for i in range(1, 5)] for role in roles}
        if len(roles) == 3:
            restored[case[0]] = restored_result_layout(*(structural[role] for role in roles))
        reference_counts = {}
        for pad in PADS:
            ab = {role: [read(role, f'pad{pad}-round{i}') for i in (1, 2)] for role in roles}
            metrics = ('min_us',) if min(float(r['median_us']) for r in ab['A']) < 10 else ('median_us', 'p95_us')
            for role in roles[1:]:
                for metric in metrics:
                    a, b, ratio, floor, verdict = compare(aa['A'], ab['A'], ab[role], metric)
                    # Keep the baseline classification, alongside the candidate's own floor.
                    _, _, _, candidate_floor, _ = compare(aa[role], ab['A'], ab[role], metric)
                    lines.append(f"| {'/'.join(case)} | {role}/A | {pad} | {metric} | {a:.4f} | {b:.4f} | "
                                 f"{display(None if ratio is None else ratio - 1, True)} | {display(floor, True)} | {display(candidate_floor, True)} | {verdict} |")
            for role in roles:
                observations = []
                for repeat in (1, 2):
                    suffix = f'count{pad}-round{repeat}'
                    read(role, suffix, count=True)
                    observations.append(counts(root, prefix(case, role, suffix)))
                if observations[0] != observations[1]:
                    raise ValueError(f'non-repeating software counts: {case}/{role}/{pad}')
                if role in reference_counts and reference_counts[role] != observations[0]:
                    raise ValueError(f'software counts drift with text placement: {case}/{role}')
                reference_counts[role] = observations[0]
                total, funcs = observations[0]
                count_rows.append(('/'.join(case), role, pad, *total))
                function_rows.extend(('/'.join(case), role, pad, name, *cost) for name, cost in sorted(funcs.items()))
        count_lines.extend(['', f"Counts {'/'.join(case)}: " + '; '.join(
            f'{role} Ir/Dr/Dw={reference_counts[role][0]}' for role in roles) + '. Both processes and pads agree.', ''])
    if restored:
        lines.extend(['', '## Result array offsets (bytes)', '',
                      '| Profile | Member | Base | Original | Revised | Member size |',
                      '| --- | --- | ---: | ---: | ---: | ---: |'])
        for profile, rows in restored.items():
            lines.extend(f'| {profile} | ' + ' | '.join(map(str, row)) + ' |' for row in rows)
        lines.extend(['', 'All pre-existing result arrays and the proof object retain baseline offsets/sizes. '
                      'This does not restore every object address or enclosing allocation, nor isolate a cache mechanism.', ''])
    lines.extend(count_lines)
    for filename, fields, rows in (
            ('layouts.csv', ('pass', 'key', 'value'), layout_rows),
            ('counts.csv', ('case', 'role', 'pad', 'Ir', 'Dr', 'Dw'), count_rows),
            ('functions.csv', ('case', 'role', 'pad', 'function', 'Ir', 'Dr', 'Dw'), function_rows)):
        with (root / filename).open('w', newline='') as out:
            writer = csv.writer(out); writer.writerow(fields); writer.writerows(rows)
    (root / 'session-layout.md').write_text('\n'.join(lines) + '\n')


if __name__ == '__main__':
    if len(sys.argv) in (5, 6) and sys.argv[1] == 'prepare':
        prepare(sys.argv[2], sys.argv[3], Path(sys.argv[4]).resolve(), sys.argv[5] if len(sys.argv) == 6 else '')
    elif len(sys.argv) == 3 and sys.argv[1] in ('run', 'report'):
        (run if sys.argv[1] == 'run' else report)(Path(sys.argv[2]).resolve())
    else:
        sys.exit('usage: diagnose_session_layout.py prepare BASE HEAD NEW_OUTPUT [ORIGINAL] | run/report OUTPUT')
