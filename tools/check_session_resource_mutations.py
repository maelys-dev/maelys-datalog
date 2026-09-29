#!/usr/bin/env python3
"""Rebuild resource-contract mutants outside Git; compilation is not detection.

The baseline and every mutant run under ASan/UBSan. Retain --output with logs
and exact patches. No production file is modified. This is a contract test,
not timing evidence or a release hook.
"""
import argparse
from concurrent.futures import ThreadPoolExecutor
import json
import os
from pathlib import Path
import subprocess

ROOT = Path(__file__).resolve().parents[1]
RUNTIME = "src/runtime/maelys_datalog_runtime.c"
RESOURCES = "src/runtime/maelys_datalog_resources.inc"
PREPARED = "src/core/maelys_datalog_prepared_session.c"
PROGRAM = "src/compiler/maelys_datalog_program.c"

# name, compiled unit, edited file, exact anchor, replacement, witnessing case
MUTATIONS = [
    ("elastic_mode_accepted", RUNTIME, RESOURCES,
     "mode != MAELYS_DATALOG_MEMORY_FIXED", "mode > MAELYS_DATALOG_MEMORY_BACKEND_ELASTIC", "records"),
    ("reserved_requirement_accepted", RUNTIME, RESOURCES,
     "(features & ~MAELYS_DATALOG_RESOURCE_SUPPORTED_014)",
     "(features & ~(MAELYS_DATALOG_RESOURCE_SUPPORTED_014 | MAELYS_DATALOG_RESOURCE_CALLER_ALLOCATOR))", "records"),
    ("reserved_provider_feature_accepted", "src/registry/maelys_datalog_modules.c", "src/registry/maelys_datalog_modules.c",
     "d->resource_features & ~MAELYS_DATALOG_RESOURCE_SUPPORTED_014",
     "d->resource_features & ~(MAELYS_DATALOG_RESOURCE_SUPPORTED_014 | MAELYS_DATALOG_RESOURCE_CALLER_ALLOCATOR)", "agreement"),
    ("commit_after_rejection", RUNTIME, RUNTIME,
     "        s->backend.destroy_result(s->state, result->state);",
     "        s->backend.commit(s->state, result->state);\n        s->backend.destroy_result(s->state, result->state);", "agreement"),
    ("quota_through_program_info", PROGRAM, PROGRAM,
     "                                           MAELYS_DATALOG_MAX_EDB_FACTS,",
     "                                           p->prepared_inputs ? p->prepared_inputs->input_capacity : MAELYS_DATALOG_MAX_EDB_FACTS,", "agreement"),
    ("abi5_nondefault_accepted", RUNTIME, RESOURCES,
     "if(v.required_features) return MAELYS_DATALOG_STATUS_UNSUPPORTED;",
     "if(0) return MAELYS_DATALOG_STATUS_UNSUPPORTED;", "defaults"),
    ("compiled_facts_charged_to_E", RUNTIME, RESOURCES,
     "p->pool += rules->fact_count < room ? rules->fact_count : room;",
     "(void)room;", "compiled_facts"),
    ("prepare_resources_differ", RUNTIME, RESOURCES,
     "s->resources = p->resources;", "s->resources = p->resources; ++s->resources.derived_facts;", "agreement"),
    ("default_identity_for_nondefault", RUNTIME, RESOURCES,
     "if (!rc && p->resources.required_features) {", "if (0) {", "defaults"),
    ("symbols_quota_ignored", PREPARED, PREPARED,
     "session->symbol_capacity = symbols;", "(void)symbols; session->symbol_capacity = MAELYS_DATALOG_MAX_SYMBOLS;", "diagnostics"),
    ("text_quota_ignored", PREPARED, PREPARED,
     "session->text_capacity = text;", "(void)text; session->text_capacity = MAELYS_DATALOG_STRING_POOL_BYTES;", "quotas"),
    ("external_D_quota_ignored", RUNTIME, RUNTIME,
     "if (result->derived.count >= s->resources.derived_facts) {", "if (0) {", "agreement"),
    ("overlap_live_provider_ignored", RUNTIME, RESOURCES,
     "resource_conflict(ranges[i],sizes[i],live->backend_bytes_start,live->backend_bytes) ||",
     "0 ||", "storage_edges"),
    ("plan_overflow_unchecked", RUNTIME, RESOURCES,
     "if (pad > SIZE_MAX - n || bytes > SIZE_MAX - n - pad || bytes > SIZE_MAX - *component)",
     "if (0)", "storage_edges"),
]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--profile", choices=("SMALL", "LARGE"), default="SMALL")
    parser.add_argument("--cc", default=os.environ.get("CC", "clang"))
    parser.add_argument("--jobs", type=int, default=4)
    args = parser.parse_args()
    output = args.output.resolve()
    if output == ROOT or ROOT in output.parents:
        parser.error("--output must be outside the checkout")
    output.mkdir(parents=True, exist_ok=True)
    sources = []
    for manifest in ("core", "standard", "native"):
        sources += [line for line in (ROOT / f"build-support/{manifest}-sources.txt").read_text().splitlines()
                    if line and not line.startswith("#")]
    flags = ["-std=c11", "-D_POSIX_C_SOURCE=200809L", "-O1", "-g", "-Wall", "-Wextra", "-Werror", "-Wno-unused-function",
             "-UNDEBUG", "-fsanitize=address,undefined", "-fno-sanitize-recover=all",
             "-fno-omit-frame-pointer", f"-DMAELYS_DATALOG_PROFILE_{args.profile}"]
    env = dict(os.environ, ASAN_OPTIONS="detect_leaks=0:halt_on_error=1",
               UBSAN_OPTIONS="halt_on_error=1:print_stacktrace=1")

    def invoke(command, log, check=True):
        with log.open("w") as stream:
            result = subprocess.run(command, cwd=ROOT, env=env, stdout=stream, stderr=subprocess.STDOUT)
        if check and result.returncode:
            raise RuntimeError(f"build/baseline failure, not a detected mutation: {log}")
        return result.returncode

    def compile_base(source):
        obj = output / "objects" / (source.replace("/", "_") + ".o")
        obj.parent.mkdir(exist_ok=True)
        invoke([args.cc, *flags, "-I.", "-Iinclude", "-c", source, "-o", str(obj)], obj.with_suffix(".log"))
        return obj

    with ThreadPoolExecutor(max_workers=args.jobs) as pool:
        objects = list(pool.map(compile_base, sources))
    test = compile_base("tests/test_maelys_datalog_session_resources.c")
    baseline = output / "baseline"
    invoke([args.cc, *flags, *map(str, objects), str(test), "-o", str(baseline)], output / "baseline-build.log")
    invoke([str(baseline)], output / "baseline-run.log")
    print("baseline PASS", flush=True)
    results = []
    for name, unit, edited, old, new, case in MUTATIONS:
        directory = output / name
        file = directory / edited
        file.parent.mkdir(parents=True, exist_ok=True)
        text = (ROOT / edited).read_text()
        if text.count(old) != 1:
            raise RuntimeError(f"mutation anchor is not unique: {name}")
        text = text.replace(old, new)
        if name == "quota_through_program_info":
            text = '#include "src/core/maelys_datalog_prepared_session_internal.h"\n' + text
        file.write_text(text)
        obj, binary = directory / "mutant.o", directory / "mutant"
        source = file if edited == unit else ROOT / unit
        invoke([args.cc, *flags, "-I" + str(directory), "-I.", "-Iinclude", "-c", str(source), "-o", str(obj)], directory / "compile.log")
        linked = [obj if original == unit else base for original, base in zip(sources, objects)]
        invoke([args.cc, *flags, *map(str, linked), str(test), "-o", str(binary)], directory / "link.log")
        code = invoke([str(binary), case], directory / "run.log", check=False)
        results.append({"mutation": name, "case": case, "exit_code": code, "detected": code != 0})
        print(name, "DETECTED" if code else "SURVIVED", flush=True)
    (output / "results.json").write_text(json.dumps(results, indent=2) + "\n")
    if not all(item["detected"] for item in results):
        raise SystemExit(1)


if __name__ == "__main__":
    main()
