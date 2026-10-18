#!/usr/bin/env python3
# SPDX-License-Identifier: MPL-2.0
"""Compile named negative controls for the 0.26 diagnostic contract, outside Git."""
import argparse
import json
from pathlib import Path
import shutil
import subprocess

ROOT = Path(__file__).resolve().parents[1]
MUTATIONS = (
    ('aggregate_author_as_malformed', 'src/compiler/maelys_datalog_validate.c',
     'if (!check_aggregate_variables) return 1;', 'if (!check_aggregate_variables) return 0;', 'aggregate_authors_never_malformed'),
    ('aggregate_as_negation_cycle', 'src/compiler/maelys_datalog_validate.c',
     '"stratification cycle", ruleset->aggregates_supported', '"negation cycle", ruleset->aggregates_supported', 'aggregate_cycle_without_negation'),
    ('symbol_count_wrong_budget', 'src/core/maelys_datalog_prepared_session.c',
     'input_capacity(diagnostic, MAELYS_DATALOG_LIMIT_MAX_SYMBOLS,', 'input_capacity(diagnostic, MAELYS_DATALOG_LIMIT_MAX_EDB_FACTS,', 'input_symbol_count_capacity'),
    ('dictionary_wrong_budget', 'src/core/maelys_datalog_prepared_session.c',
     'input_capacity(diagnostic, MAELYS_DATALOG_LIMIT_STRING_POOL_BYTES,', 'input_capacity(diagnostic, MAELYS_DATALOG_LIMIT_INPUT_EDB_TEXT_BYTES,', 'input_dictionary_bytes_capacity'),
    ('long_symbol_wrong_budget', 'src/core/maelys_datalog_prepared_session.c',
     'input_capacity(diagnostic, MAELYS_DATALOG_LIMIT_MAX_STRING_BYTES,', 'input_capacity(diagnostic, MAELYS_DATALOG_LIMIT_MAX_SYMBOLS,', 'input_symbol_length_status_and_capacity'),
    ('long_symbol_invalid_argument', 'src/core/maelys_datalog_prepared_session.c',
     'MAELYS_DATALOG_MAX_STRING_BYTES + 1u, MAELYS_DATALOG_MAX_STRING_BYTES);\n                        return MAELYS_ERR_PAYLOAD_TOO_LARGE;',
     'MAELYS_DATALOG_MAX_STRING_BYTES + 1u, MAELYS_DATALOG_MAX_STRING_BYTES);\n                        return MAELYS_ERR_INVALID_ARGUMENT;', 'input_symbol_length_status_and_capacity'),
    ('ordinary_rule_as_or', 'src/core/maelys_datalog_parser.c',
     '"rule capacity exceeded", "reduce the number of rules or split the policy"',
     '"OR expansion exceeds rule capacity", "reduce OR alternatives or split the policy"', 'ordinary_rule_capacity_message'),
    ('depth_message_is_code', 'src/core/maelys_datalog_diagnostic.c',
     '"recursive evaluation exceeds iteration depth limit"', '"max_depth"', 'recursion_depth_wording'),
    ('not_found_bare_status', 'src/runtime/maelys_datalog_runtime.c',
     'if (!rc || !d) return rc;', 'if (!rc || !d || rc == MAELYS_DATALOG_STATUS_NOT_FOUND) return rc;', 'explanation_diagnosed_failures'),
    ('unreserved_kind_as_backend', 'src/runtime/maelys_datalog_runtime.c',
     '"explanation kind not reserved at prepare"', '"explanation kind not supported by the backend"', 'explanation_diagnosed_failures'),
)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--profile', choices=('small', 'large'), required=True)
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    output = args.output.resolve()
    if output == ROOT or ROOT in output.parents or output.exists():
        parser.error('output must be a new directory outside Git')
    output.mkdir(parents=True)
    flags = '-Wall -Wextra -Werror -g -I. -Iinclude'
    if args.profile == 'large':
        flags += ' -DMAELYS_DATALOG_PROFILE_LARGE'
    summary = []
    for name, file, old, new, case in (('baseline', '', '', '', ''),) + MUTATIONS:
        source = output / name / 'source'
        shutil.copytree(ROOT, source, ignore=shutil.ignore_patterns('.git', 'build', '__pycache__', '*.pyc'))
        if file:
            path = source / file
            text = path.read_text()
            assert text.count(old) == 1, (name, text.count(old))
            path.write_text(text.replace(old, new))
        build = output / name / 'build'
        command = ['make', '-j2', f'BUILD_DIR={build}', f'CFLAGS={flags}',
                   str(build / 'tests/test_maelys_datalog_diagnostic_wording')]
        with (output / (name + '-build.log')).open('w') as log:
            log.write(json.dumps(command) + '\n'); log.flush()
            subprocess.run(command, cwd=source, stdout=log, stderr=subprocess.STDOUT, check=True)
        command = [str(build / 'tests/test_maelys_datalog_diagnostic_wording')]
        if case:
            command.append(case)
        result = subprocess.run(command, cwd=source, capture_output=True, text=True)
        (output / (name + '-test.log')).write_text(result.stdout + result.stderr)
        expected = 1 if case else 0
        assert result.returncode == expected, (name, result.returncode, result.stderr)
        if case:
            assert 'tests[i].run()' in result.stderr, (name, result.stderr)
        summary.append(dict(variant=name, case=case, exit_code=result.returncode, expected=expected))
        (output / 'summary.json').write_text(json.dumps(summary, indent=2) + '\n')
        print(args.profile, name, 'DETECTED' if case else 'PASS', flush=True)


if __name__ == '__main__':
    main()
