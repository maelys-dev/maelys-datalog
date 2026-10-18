#!/usr/bin/env python3
# SPDX-License-Identifier: MPL-2.0
"""Compare every corpus fixture against a fixed base and kill first-letter mutants.

Evidence stays outside Git. Both trees use the candidate's observation harness,
with the original corpus and engine from the requested base as the reference.
"""
import argparse
import os
from pathlib import Path
import shutil
import subprocess
import tarfile

ROOT = Path(__file__).resolve().parents[1]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--base', default='f1632b5e39ea2f3e85d743fa260736ae884af7c0')
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--profile', choices=['small', 'large'], required=True)
    args = parser.parse_args()
    output = args.output.resolve()
    if output.exists() or output == ROOT or ROOT in output.parents:
        parser.error('output must be a new directory outside Git')
    output.mkdir(parents=True)
    base = output / 'base'
    base.mkdir()
    archive = output / 'base.tar'
    with archive.open('wb') as stream:
        subprocess.run(['git', 'archive', args.base], cwd=ROOT, stdout=stream, check=True)
    with tarfile.open(archive) as stream:
        stream.extractall(base, filter='data')
    harness = 'tests/test_maelys_datalog_corpus.c'
    shutil.copy2(ROOT / harness, base / harness)
    flags = '-Wall -Wextra -g -I. -Iinclude' + (' -DMAELYS_DATALOG_PROFILE_LARGE' if args.profile == 'large' else '')

    def run(tree, target, label, snapshot=False):
        build = output / (label + '-build')
        with (output / (label + '-build.log')).open('w') as log:
            subprocess.run(['make', '-j4', f'BUILD_DIR={build}', f'CFLAGS={flags}',
                            str(build / 'tests' / target)], cwd=tree, stdout=log,
                           stderr=subprocess.STDOUT, check=True)
        env = dict(os.environ, MAELYS_CORPUS_FINGERPRINTS='1') if snapshot else os.environ
        result = subprocess.run([str(build / 'tests' / target)], cwd=tree,
                                env=env, capture_output=True, text=True)
        (output / (label + '.log')).write_text(result.stdout + result.stderr)
        return result

    snapshots = []
    for tree, label in [(base, 'before'), (ROOT, 'after')]:
        result = run(tree, 'test_maelys_datalog_corpus', label, True)
        assert result.returncode == 0, result.stderr
        snapshots.append({line.split()[1]: line.split()[2:] for line in result.stdout.splitlines()
                          if line.startswith('CORPUS ')})
    before, after = snapshots
    assert before and before.keys() == after.keys()
    unchanged = changed = rejected = 0
    for fixture, old in sorted(before.items()):
        new = after[fixture]
        assert old[:2] == new[:2], (fixture, old, new)
        if old[1] != '0':
            rejected += 1
        elif old[0] == '0':
            assert old == new, (fixture, old, new)
            unchanged += 1
        elif old != new:
            changed += 1
    print(f'{args.profile}: {unchanged} single-letter/no-variable fixtures unchanged; '
          f'{changed} long-name fixtures changed; {rejected} rejected fixtures preserved', flush=True)
    result = run(ROOT, 'test_maelys_datalog_variable_names', 'positive')
    assert result.returncode == 0, result.stderr
    for site, old, new in [
        ('term', 'maelys_result_t rc = named_variable(p, &term->as.variable);',
         "maelys_result_t rc = MAELYS_OK; term->as.variable = (unsigned)(p->tok.text[0] - 'A');"),
        ('filter', 'rc = named_variable(p, &value.as.variable);',
         "rc = MAELYS_OK; value.as.variable = (unsigned)(p->tok.text[0] - 'A');"),
    ]:
        tree = output / site
        shutil.copytree(ROOT, tree, ignore=shutil.ignore_patterns('.git', 'build', '__pycache__', '*.pyc'))
        path = tree / 'src/core/maelys_datalog_parser.c'
        text = path.read_text()
        assert text.count(old) == 1
        path.write_text(text.replace(old, new))
        result = run(tree, 'test_maelys_datalog_variable_names', site)
        assert result.returncode == 1 and 'line ' in result.stderr, result.stderr
        print(f'{args.profile}: {site} first-letter mutant detected', flush=True)


if __name__ == '__main__':
    main()
