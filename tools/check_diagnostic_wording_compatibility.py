#!/usr/bin/env python3
# SPDX-License-Identifier: MPL-2.0
"""Compare corpus acceptance, fingerprints and seeded solve output to v0.25.0.

Identical observation code is copied into both snapshots. Results and logs remain
outside Git. The typed seed is bounded evidence, not exhaustive policy inputs.
"""
import argparse
import json
import re
from pathlib import Path
import shutil
import subprocess
import tarfile

ROOT = Path(__file__).resolve().parents[1]
BASE = '0d61a4b848e5a94a0a352124989cd7ae1b672dec'


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--profile', choices=('small', 'large'), required=True)
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    output = args.output.resolve()
    if output == ROOT or ROOT in output.parents or output.exists():
        parser.error('output must be a new directory outside Git')
    output.mkdir(parents=True)
    base = output / 'base'; base.mkdir()
    with (output / 'base.tar').open('wb') as stream:
        subprocess.run(['git', 'archive', BASE], cwd=ROOT, stdout=stream, check=True)
    with tarfile.open(output / 'base.tar') as stream:
        stream.extractall(base, filter='data')
    header = 'include/maelys/datalog.h'
    for enum in ('maelys_datalog_diag_code_t', 'maelys_datalog_limit_t'):
        pattern = r'typedef enum \{([^{}]*)\} ' + enum + ';'
        before = re.search(pattern, (base / header).read_text()).group(1)
        after = re.search(pattern, (ROOT / header).read_text()).group(1)
        assert before == after, enum + ' values changed'
    candidate = output / 'candidate'
    shutil.copytree(ROOT, candidate, ignore=shutil.ignore_patterns('.git', 'build', '__pycache__', '*.pyc'))
    # Extend the existing typed-seed oracle with a stable semantic observation,
    # never bytes of a native struct (which could contain padding/pointers).
    helper = (ROOT / 'tests/helpers/solve_validated_seed.h').read_text()
    marker = '    if (rc == MAELYS_OK) {\n        if (!result) abort();'
    observation = '''    printf("SOLVE %d", (int)rc);
    if (rc == MAELYS_OK) {
        const maelys_datalog_fact_set_t *view = maelys_datalog_solve_result_derived_view(result);
        if (!view) abort();
        for (size_t i = 0; i < view->count; ++i) {
            const maelys_datalog_internal_fact_t *f = &view->facts[i];
            printf(" %u/%u", f->predicate_id, f->arity);
            for (size_t t = 0; t < f->arity; ++t) {
                maelys_datalog_internal_term_t term = maelys_datalog_fact_term(f, t);
                printf(":%d/", (int)term.kind);
                if (term.kind == MAELYS_DATALOG_TERM_SYMBOL)
                    printf("s%u", term.as.symbol);
                else if (term.kind == MAELYS_DATALOG_TERM_INT)
                    printf("i%lld", (long long)term.as.integer);
                else printf("b%d", term.as.boolean);
            }
        }
    }
    putchar('\\n');
'''
    assert marker in helper
    helper = helper.replace(marker, observation + marker)
    helper = '#include \"src/core/maelys_datalog_solver_internal.h\"\n' + helper
    snapshots = []
    for label, tree in [('before', base), ('after', candidate)]:
        (tree / 'tests/helpers/solve_validated_seed.h').write_text(helper)
        shutil.copy2(ROOT / 'tests/test_maelys_datalog_corpus.c', tree / 'tests/test_maelys_datalog_corpus.c')
        build = output / (label + '-build')
        flags = '-Wall -Wextra -g -I. -Iinclude' + (' -DMAELYS_DATALOG_PROFILE_LARGE' if args.profile == 'large' else '')
        with (output / (label + '-build.log')).open('w') as log:
            subprocess.run(['make', '-j2', f'BUILD_DIR={build}', f'CFLAGS={flags}',
                            str(build / 'tests/test_maelys_datalog_corpus')], cwd=tree,
                           stdout=log, stderr=subprocess.STDOUT, check=True)
        import os
        result = subprocess.run([str(build / 'tests/test_maelys_datalog_corpus')], cwd=tree,
                                env=dict(os.environ, MAELYS_CORPUS_FINGERPRINTS='1'), capture_output=True, text=True)
        (output / (label + '.log')).write_text(result.stdout + result.stderr)
        assert result.returncode == 0, result.stderr
        snapshots.append([line for line in result.stdout.splitlines() if line.startswith(('CORPUS ', 'SOLVE '))])
    assert snapshots[0] and snapshots[0] == snapshots[1], 'corpus status, fingerprint or result changed'
    cases = [line.split() for line in snapshots[0] if line.startswith('CORPUS ')]
    summary = dict(base=BASE, profile=args.profile, fixtures=len(cases),
                   accepted=sum(row[3] == '0' for row in cases),
                   rejected=sum(row[3] != '0' for row in cases),
                   fingerprints_and_seeded_results='identical')
    (output / 'summary.json').write_text(json.dumps(summary, indent=2) + '\n')
    print(json.dumps(summary), flush=True)


if __name__ == '__main__':
    main()
