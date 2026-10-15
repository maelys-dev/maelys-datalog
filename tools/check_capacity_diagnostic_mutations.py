#!/usr/bin/env python3
# SPDX-License-Identifier: MPL-2.0
"""Rebuild capacity diagnostic negative controls against the named-budget oracle."""
import argparse
from pathlib import Path
import shutil
import subprocess

ROOT = Path(__file__).resolve().parents[1]
MUTATIONS = (
    ("zero_lexer_budget", "src/core/maelys_datalog_lexer.c",
     "l->diag, MAELYS_DATALOG_LIMIT_MAX_STRING_BYTES,", "l->diag, 0,", "lexer string +1"),
    ("rules_report_literals_budget", "src/core/maelys_datalog_parser.c",
     "p->diag, MAELYS_DATALOG_LIMIT_MAX_RULES,", "p->diag, MAELYS_DATALOG_LIMIT_MAX_BODY_LITERALS,", "rules_129_names_MAX_RULES"),
    ("direct_fact_bare_return", "src/core/maelys_datalog_parser.c",
     """        if (p->ruleset->fact_count >= MAELYS_DATALOG_MAX_RULE_FACTS) {
            parser_diag(p,
                        MAELYS_DATALOG_DIAG_OPERATION_REJECTED,
                        "direct facts exceed policy fact capacity",
                        "reduce the number of direct policy facts");
            maelys_datalog_internal_diagnostic_set_limit(p->diag,
                MAELYS_DATALOG_LIMIT_MAX_RULE_FACTS,
                p->ruleset->fact_count + 1u, MAELYS_DATALOG_MAX_RULE_FACTS);
            return MAELYS_ERR_PAYLOAD_TOO_LARGE;
        }""",
     '        if (p->ruleset->fact_count >= MAELYS_DATALOG_MAX_RULE_FACTS) return MAELYS_ERR_PAYLOAD_TOO_LARGE;', "direct_facts_129_names_MAX_RULE_FACTS"),
)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--output", required=True, type=Path)
    parser.add_argument("--profile", choices=("SMALL", "LARGE"), default="SMALL")
    args = parser.parse_args()
    output = args.output.resolve()
    if output == ROOT or ROOT in output.parents:
        parser.error("evidence must stay outside Git")
    output.mkdir(parents=True, exist_ok=True)
    for name, file, old, new, marker in (("baseline", "", "", "", ""),) + MUTATIONS:
        source = output / name / "source"
        if source.exists():
            parser.error(f"refusing to overwrite {source}")
        shutil.copytree(ROOT, source, ignore=shutil.ignore_patterns(".git", "build", "__pycache__", "*.pyc"))
        if file:
            path = source / file
            text = path.read_text()
            assert text.count(old) == 1, (name, text.count(old))
            path.write_text(text.replace(old, new))
        build = output / name / "build"
        with (output / name / "build.log").open("w") as log:
            subprocess.run(["cmake", "-S", str(source), "-B", str(build),
                "-DCMAKE_BUILD_TYPE=Debug", "-DCMAKE_C_COMPILER=clang",
                "-DCMAKE_C_FLAGS=-fsanitize=address,undefined -fno-sanitize-recover=all -fno-omit-frame-pointer",
                "-DMAELYS_DATALOG_PROFILE_LARGE=" + ("ON" if args.profile == "LARGE" else "OFF")],
                stdout=log, stderr=subprocess.STDOUT, check=True)
            subprocess.run(["cmake", "--build", str(build), "--target", "test_capacity_diagnostics", "-j2"],
                stdout=log, stderr=subprocess.STDOUT, check=True)
        result = subprocess.run([str(build / "test_capacity_diagnostics")], capture_output=True, text=True)
        (output / name / "test.log").write_text(result.stdout + result.stderr)
        if name == "baseline":
            assert result.returncode == 0, result.stderr
        else:
            assert result.returncode == 1 and marker in result.stderr, (name, result.returncode, result.stderr)
            assert "AddressSanitizer" not in result.stderr and "runtime error:" not in result.stderr
        print(f"{args.profile} {name}: PASS", flush=True)


if __name__ == "__main__":
    main()
