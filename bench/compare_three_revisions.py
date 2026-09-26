#!/usr/bin/env python3
# SPDX-License-Identifier: MPL-2.0
"""Full matrices in one sequential A/B/C run; never build between timed passes."""
import hashlib
import json
import os
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile

DRIVER = Path(__file__).resolve().parent.parent
PROFILES = ('SMALL', 'LARGE')
ROLES = ('A', 'B', 'C')
KINDS = ('solver', 'input', 'sessions')
PAIRS = (('base-original', 'A', 'B'), ('base-revised', 'A', 'C'), ('original-revised', 'B', 'C'))


def main(base, original, head, root):
    if subprocess.check_output(['uname', '-s'], text=True).strip() != 'Linux':
        raise ValueError('Linux required; local containers are tooling smoke only')
    def resolve(ref):
        for name in (ref, f'refs/remotes/origin/{ref}'):
            r = subprocess.run(['git', '-C', str(DRIVER), 'rev-parse', '--verify', '--end-of-options', name + '^{commit}'],
                               text=True, capture_output=True)
            if r.returncode == 0:
                return r.stdout.strip()
        raise ValueError(f'unknown revision: {ref}')
    revisions = dict(zip(ROLES, map(resolve, (base, original, head))))
    root.mkdir(exist_ok=False)
    work = Path(tempfile.mkdtemp(prefix='datalog-compare-three-'))
    (root / 'workspace.txt').write_text(str(work) + '\n')
    (root / 'revisions.json').write_text(json.dumps(revisions, indent=2) + '\n')
    (root / 'protocol.txt').write_text(
        'A=base B=original C=revised; all six revision/profile builds before timing.\n'
        'Two A/A pairs for every role/profile, then A B C / C B A.\n'
        'Every solver/input/session case retained; no priority or affinity changes.\n'
        'Pair reports reuse those same raw observations; they are not separate campaigns.\n')
    def command(args, output=None, env=None):
        args = list(map(str, args))
        with (root / 'commands.log').open('a') as log:
            print('+', ' '.join(args), flush=True)
            log.write('+ ' + ' '.join(args) + '\n'); log.flush()
            if output:
                with output.open('w') as out:
                    subprocess.run(args, check=True, stdout=out, stderr=log, env=env)
            else:
                subprocess.run(args, check=True, stdout=log, stderr=log, env=env)
    for args in (['uname', '-a'], ['clang', '--version'], ['getconf', '_NPROCESSORS_ONLN']):
        command(args)
    # Metadata at the root also describes the complete base/revised comparison.
    def metadata(destination, a, b):
        command(['python3', DRIVER / 'bench/compare_runs.py', 'metadata', destination, a, b, DRIVER])
        path = destination / 'metadata.json'
        data = json.loads(path.read_text())
        data['comparison_protocol'] = (
            'Two A/A pairs for each revision precede the common A B C / C B A campaign '
            '(A=base, B=original, C=revised). This pair report relabels its baseline/candidate '
            'as A/B and reuses the common observations and its baseline A/A floor.')
        path.write_text(json.dumps(data, indent=2) + '\n')
    metadata(root, revisions['A'], revisions['C'])
    (root / 'harness.sha256').write_text(''.join(
        f'{hashlib.sha256(p.read_bytes()).hexdigest()}  {p.name}\n'
        for p in sorted((DRIVER / 'bench').iterdir()) if p.is_file()))
    expected = subprocess.check_output(['git', '-C', str(DRIVER), 'hash-object', 'bench/bench_datalog.c'], text=True).strip()
    env = dict(os.environ)
    env.pop('MAKEFLAGS', None); env.pop('MFLAGS', None)
    diagnostics = env.get('SESSION_DIAGNOSTICS', '0')
    if diagnostics not in ('0', '1'):
        raise ValueError('SESSION_DIAGNOSTICS must be 0 or 1')
    explanations = False
    binaries = {}
    for role, revision in revisions.items():
        actual = subprocess.check_output(['git', '-C', str(DRIVER), 'rev-parse', revision + ':bench/bench_datalog.c'], text=True).strip()
        if actual != expected:
            raise ValueError(f'solver harness differs: {revision}')
        source = work / role; source.mkdir()
        archive = work / (role + '.tar')
        command(['git', '-C', DRIVER, 'archive', '--output', archive, revision])
        command(['tar', '-xf', archive, '-C', source])
        if role == 'C':
            explanations = 'maelys_datalog_session_config_set_explanation_workspace(' in (source / 'include/maelys/datalog.h').read_text()
        for profile in PROFILES:
            out = work / f'bin-{role}-{profile}'
            command(['make', '-j1', '-C', source, '-f', DRIVER / 'bench/Makefile.compare',
                     f'DRIVER={DRIVER}', f'OUT={out}', f'REVISION={revision}', f'PROFILE={profile}',
                     f'EXPLANATIONS={int(explanations)}', f'SESSION_DIAGNOSTICS={diagnostics}'], env=env)
            for kind in (*KINDS, *(('explanations',) if explanations else ()),
                         *(('sessions-counts',) if diagnostics == '1' else ())):
                binary = out / kind
                binaries[str(binary)] = hashlib.sha256(binary.read_bytes()).hexdigest()
    (root / 'binaries.json').write_text(json.dumps(binaries, indent=2) + '\n')
    # No compilation beyond this point. All actual output checking is performed
    # by the unchanged harnesses outside their timed regions.
    def measure(profile, role, suffix):
        for kind in KINDS:
            path = root / f'{profile}-{kind}-{suffix}'
            sample_env = dict(env, MAELYS_BENCH_SAMPLES='1000')
            command([work / f'bin-{role}-{profile}' / kind, str(path) + '.csv',
                     str(path) + ('.json' if kind == 'solver' else '.samples.csv')], env=sample_env)
    def explain(profile, mode, suffix):
        if explanations:
            path = root / f'{profile}-explanations-{suffix}'
            command([work / f'bin-C-{profile}' / 'explanations', mode,
                     str(path) + '.csv', str(path) + '.samples.csv'])
    for profile in PROFILES:
        for role in ROLES:
            for repeat in range(1, 5):
                measure(profile, role, f'aa-{role}{repeat}')
        for repeat in range(1, 5):
            explain(profile, 'legacy', f'aa-{repeat}')
    for profile in PROFILES:
        for repeat, roles in ((1, ROLES), (2, tuple(reversed(ROLES)))):
            for role in roles:
                measure(profile, role, f'ab-{role}{repeat}')
        for repeat in (1, 2):
            explain(profile, 'legacy', f'ab-A{repeat}')
            explain(profile, 'workspace', f'ab-B{repeat}')
    for name, a, b in PAIRS:
        destination = root / name; destination.mkdir()
        metadata(destination, revisions[a], revisions[b])
        for profile in PROFILES:
            for kind in KINDS:
                mapping = [(f'aa-{a}{i}', f'aa-{i}') for i in range(1, 5)]
                mapping += [(f'ab-{role}{i}', f'ab-{label}{i}') for label, role in (('A', a), ('B', b)) for i in (1, 2)]
                for source, target in mapping:
                    for extension in ('.csv', '.json' if kind == 'solver' else '.samples.csv'):
                        shutil.copy2(root / f'{profile}-{kind}-{source}{extension}', destination / f'{profile}-{kind}-{target}{extension}')
        for script, report in (('compare_runs.py', 'comparison.md'), ('compare_sessions.py', 'sessions.md')):
            command(['python3', DRIVER / 'bench' / script, destination], output=destination / report)
        if name == 'base-revised':
            (destination / 'explanations-enabled.txt').write_text(str(int(explanations)) + '\n')
            for p in root.glob('*-explanations-*'):
                shutil.copy2(p, destination / p.name)
            command(['python3', DRIVER / 'bench/compare_explanations.py', destination], output=destination / 'explanations.md')
        if diagnostics == '1':
            pair_work = work / name; pair_work.mkdir()
            for label, role in (('A', a), ('B', b)):
                (pair_work / label).symlink_to(work / role, target_is_directory=True)
                for profile in PROFILES:
                    (pair_work / f'bin-{label}-{profile}').symlink_to(work / f'bin-{role}-{profile}', target_is_directory=True)
            command(['python3', DRIVER / 'bench/diagnose_sessions.py', destination, pair_work], output=destination / 'sessions-diagnostic.md')
    (root / 'complete').write_text('All three pair reports validated.\n')


if __name__ == '__main__':
    if len(sys.argv) != 5 or not Path(sys.argv[4]).is_absolute():
        sys.exit('usage: compare_three_revisions.py BASE ORIGINAL REVISED NEW_ABSOLUTE_OUTPUT')
    main(*sys.argv[1:4], Path(sys.argv[4]))
