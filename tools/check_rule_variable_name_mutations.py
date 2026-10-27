#!/usr/bin/env python3
# SPDX-License-Identifier: MPL-2.0
"""Kill source-name contract mutants; keep all generated evidence outside Git."""
import argparse
from pathlib import Path
import shutil
import subprocess

ROOT = Path(__file__).resolve().parents[1]
MUTANTS = (
    ('slot', 'src/core/maelys_datalog_parser.c',
     'rule_variable_name_offsets[staged_base][p->variable_slots[j]]',
     'rule_variable_name_offsets[staged_base][j]'),
    ('or_scope', 'src/compiler/maelys_datalog_program.c',
     'if (!row || !(named_rule_mask(&r->rules[rule_index]) & (UINT32_C(1) << variable)))',
     'if (!row || (named_rule_mask(&r->rules[rule_index]) & 0u))'),
    ('saturation', 'src/core/maelys_datalog_parser.c',
     'if (bytes > sizeof(p->ruleset->rule_variable_name_pool) - used) continue;',
     'if (bytes > sizeof(p->ruleset->rule_variable_name_pool) - used) return MAELYS_ERR_PAYLOAD_TOO_LARGE;'),
    ('partial_pool', 'src/core/maelys_datalog_parser.c',
     'if (bytes > sizeof(p->ruleset->rule_variable_name_pool) - used) continue;',
     'if (bytes > sizeof(p->ruleset->rule_variable_name_pool) - used) { p->ruleset->rule_variable_name_pool_used = sizeof(p->ruleset->rule_variable_name_pool); continue; }'),
)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--profile', choices=('SMALL', 'LARGE'), required=True)
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    out = args.output.resolve()
    if out.exists() or out == ROOT or ROOT in out.parents:
        parser.error('output must be new and outside Git')
    out.mkdir(parents=True)
    for label, filename, old, new in MUTANTS:
        tree = out / label
        shutil.copytree(ROOT, tree, ignore=shutil.ignore_patterns('.git', 'build', '__pycache__'))
        path = tree / filename
        source = path.read_text()
        assert source.count(old) == 1, label
        path.write_text(source.replace(old, new))
        flags = '-Wall -Wextra -g -I. -Iinclude'
        if args.profile == 'LARGE':
            flags += ' -DMAELYS_DATALOG_PROFILE_LARGE'
        target = 'build/tests/test_maelys_datalog_rule_variable_names'
        with (out / (label + '-build.log')).open('w') as log:
            subprocess.run(['make', '-j4', 'CFLAGS=' + flags, target], cwd=tree,
                           stdout=log, stderr=subprocess.STDOUT, check=True)
        result = subprocess.run([str(tree / target)], cwd=tree, text=True, capture_output=True)
        (out / (label + '.log')).write_text(result.stdout + result.stderr)
        assert result.returncode == 1 and 'test_maelys_datalog_rule_variable_names.c:' in result.stderr, label
        print(args.profile, label, 'detected', flush=True)


if __name__ == '__main__':
    main()
