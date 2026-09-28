#!/usr/bin/env python3
# SPDX-License-Identifier: MPL-2.0
"""Add a separate null cross-check to an existing schema-4 report, without measuring."""
import argparse
import hashlib
import json
from pathlib import Path
import sys

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'bench'))
import python_perf as perf


def run(report_path, output):
    source = report_path.read_bytes()
    report = json.loads(source)
    diagnostic = perf.null_cross_check(report)
    digest = hashlib.sha256(source).hexdigest()
    supplement = dict(schema=1, release_eligible=False, measurement_performed=False,
                      source_report_sha256=digest, source_status=report['status'],
                      source_run_id=report['environment']['run_id'], commits=report['commits'],
                      positive_control_detected=report['positive_control']['detected'],
                      analysis_harness_sha256=perf.sha256(ROOT / 'bench/python_perf.py'),
                      analysis_tool_sha256=perf.sha256(Path(__file__)),
                      null_cross_check=diagnostic)
    # Never overwrite a historical report or an earlier supplement.
    output.mkdir(parents=True, exist_ok=False)
    (output / 'null-cross.json').write_text(json.dumps(supplement, indent=2) + '\n')
    lines = ['# Offline Python null cross-check', '',
             f"Source run: `{supplement['source_run_id']}`; candidate: `{report['commits']['head']}`.",
             f"Source report SHA-256: `{digest}`.",
             f"Original status remains **{report['status']}**; positive-control detection: **{supplement['positive_control_detected']}**.",
             'No measurement, build or source-report rewrite was performed.', '']
    (output / 'null-cross.md').write_text('\n'.join(lines + perf.null_cross_markdown(diagnostic)) + '\n')
    print(f'Null cross-check: {output}; source sha256={digest}')


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--report', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True, help='new directory for the separate supplement')
    args = parser.parse_args()
    run(args.report, args.output)
