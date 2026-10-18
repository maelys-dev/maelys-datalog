#!/usr/bin/env python3
# SPDX-License-Identifier: MPL-2.0
"""Prove the public diagnostic contract rejects two plausible regressions.

Build isolated copies outside Git; retain commands, compiler/test logs and summary
in --output. This is a semantic mutation check, not a performance measurement.
"""
import argparse
import json
from pathlib import Path
import shutil
import subprocess


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--profile', choices=('small', 'large'), default='small')
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    root = Path(__file__).resolve().parents[1]
    output = args.output.resolve()
    if output == root or root in output.parents:
        parser.error('--output must be outside the source tree')
    output.mkdir(parents=True, exist_ok=False)
    variants = ('baseline', 'premature_stratum_ceiling', 'lost_arithmetic_reason')
    report = []
    for variant in variants:
        source = output / variant / 'source'
        build = output / variant / 'build'
        shutil.copytree(root, source, ignore=shutil.ignore_patterns(
            '.git', 'build', '__pycache__', '*.pyc', 'node_modules', '.venv'))
        if variant == 'premature_stratum_ceiling':
            path = source / 'src/compiler/maelys_datalog_validate.c'
            text = path.read_text()
            marker = '            if (s > ruleset->strata[hpid]) {'
            assert text.count(marker) == 1
            text = text.replace(marker, '''            if (s >= MAELYS_DATALOG_MAX_STRATA) {
                validation_diag(p, MAELYS_DATALOG_DIAG_POLICY_NOT_STRATIFIABLE,
                    "negation depth exceeds stratum limit", "reduce depth");
                maelys_datalog_internal_diagnostic_set_limit(p->diag,
                    MAELYS_DATALOG_LIMIT_MAX_STRATA, (size_t)s + 1u, MAELYS_DATALOG_MAX_STRATA);
                return MAELYS_ERR_INVALID_FIELD;
            }
''' + marker)
            path.write_text(text)
        elif variant == 'lost_arithmetic_reason':
            path = source / 'src/core/maelys_datalog_solver.c'
            text = path.read_text()
            marker = 'diag->compare_result = result->runtime_diag.compare_result;'
            assert text.count(marker) == 1
            path.write_text(text.replace(marker, 'diag->compare_result = 0;'))
        commands = [
            ['cmake', '-S', str(source), '-B', str(build), '-DCMAKE_BUILD_TYPE=Debug',
             '-DMAELYS_DATALOG_PROFILE_LARGE=' + ('ON' if args.profile == 'large' else 'OFF')],
            ['cmake', '--build', str(build), '--target', 'test_stratum_arithmetic_diagnostics', '-j4'],
        ]
        with (output / (variant + '.log')).open('w') as log:
            for command in commands:
                log.write(json.dumps(command) + '\n'); log.flush()
                subprocess.run(command, stdout=log, stderr=subprocess.STDOUT, check=True)
            command = [str(build / 'test_stratum_arithmetic_diagnostics')]
            if variant == 'premature_stratum_ceiling':
                command.append('--cycle')
            log.write(json.dumps(command) + '\n'); log.flush()
            result = subprocess.run(command, stdout=log, stderr=subprocess.STDOUT)
        # A signal/crash is not an assertion detecting the intended regression.
        expected = 0 if variant == 'baseline' else 1
        entry = dict(variant=variant, exit_code=result.returncode, expected=expected)
        report.append(entry)
        (output / 'summary.json').write_text(json.dumps(report, indent=2) + '\n')
        if result.returncode != expected:
            raise SystemExit(f'{variant}: unexpected exit {result.returncode}; inspect log')
        print(f'{variant}: ' + ('PASS' if expected == 0 else 'DETECTED'), flush=True)


if __name__ == '__main__':
    main()
