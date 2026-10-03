#!/usr/bin/env python3
"""Rebuild caller-owned policy liveness controls under ASan/UBSan.

Only the named liveness witness runs. Compilation failure is not detection;
each mutant must fail its own policy_id or policy_stat_get assertion. Source,
build logs and run logs stay outside Git, and the checkout is never patched.
"""
import argparse
import json
import os
from pathlib import Path
import shutil
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]
UNIT = Path("src/public/maelys_datalog_api.c")
GUARD = "if (!maelys_datalog_policy_is_live(policy))"


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--profile", choices=("SMALL", "LARGE"), default="SMALL")
    parser.add_argument("--cc", default="clang")
    args = parser.parse_args()
    output = args.output.resolve()
    if output == ROOT or ROOT in output.parents:
        parser.error("output must be outside the checkout")
    output.mkdir(parents=True, exist_ok=True)
    results = []
    for operation in (None, "policy_id", "policy_stat_get"):
        name = "baseline" if operation is None else operation + "-omits-references"
        with tempfile.TemporaryDirectory(prefix="policy-liveness-") as temporary:
            tree = Path(temporary) / "source"
            shutil.copytree(ROOT, tree, ignore=shutil.ignore_patterns(".git", "build", "dist", "__pycache__"))
            if operation:
                unit = tree / UNIT
                source = unit.read_text()
                start = source.index("maelys_datalog_status_t maelys_datalog_" + operation + "(")
                end = source.index("\n}\n", start) + 3
                body = source[start:end]
                if body.count(GUARD) != 1:
                    raise SystemExit("ambiguous mutation: " + operation)
                body = body.replace(GUARD, "if (policy->released)")
                source = source[:start] + body + source[end:]
                unit.write_text(source)
                (output / (name + ".c")).write_text(source)
            flags = "-std=c11 -D_POSIX_C_SOURCE=200809L -Wall -Wextra -g -O1 -I. -Iinclude -UNDEBUG "
            flags += "-fsanitize=address,undefined,alignment -fno-sanitize-recover=all -fno-omit-frame-pointer "
            flags += "-DMAELYS_DATALOG_PROFILE_" + args.profile
            command = ["make", "build/tests/test_maelys_datalog_hot_path_alloc", "CC=" + args.cc, "CFLAGS=" + flags]
            built = subprocess.run(command, cwd=tree, capture_output=True)
            (output / (name + "-build.log")).write_bytes(built.stdout + built.stderr)
            built.check_returncode()
            env = dict(os.environ, ASAN_OPTIONS="detect_leaks=0:halt_on_error=1", UBSAN_OPTIONS="halt_on_error=1")
            ran = subprocess.run([str(tree / "build/tests/test_maelys_datalog_hot_path_alloc"),
                                  "--caller-owned-policy-liveness"], env=env, capture_output=True)
            log = ran.stdout + ran.stderr
            (output / (name + ".log")).write_bytes(log)
            if operation is None:
                assert ran.returncode == 0, "baseline failed"
            else:
                assert ran.returncode != 0 and ("maelys_datalog_" + operation + "(").encode() in log, \
                    "mutation survived or failed outside its assertion: " + operation
            results.append({"name": name, "returncode": ran.returncode, "validated": True})
            print("PASS " + name + (": detected" if operation else ": baseline"), flush=True)
    (output / "verification.json").write_text(json.dumps({"profile": args.profile, "results": results}, indent=2) + "\n")


if __name__ == "__main__":
    main()
