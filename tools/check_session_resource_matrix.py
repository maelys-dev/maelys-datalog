#!/usr/bin/env python3
# SPDX-License-Identifier: MPL-2.0
"""Separate caller/provider/host builds from two clean installed SDKs.

The old SDK is the pre-ABI-6 base, not a forged older ABI-6 implementation.
Optional-tail fixtures model compatible record evolution within ABI 6 V1.
All commands, results, object/library hashes and negative link logs are retained.
"""
import argparse
import hashlib
import json
import os
from pathlib import Path
import shlex
import shutil
import subprocess
import sys


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--old", type=Path, required=True)
    parser.add_argument("--new", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    root = Path(__file__).resolve().parents[1]
    out = args.output.resolve()
    out.mkdir(parents=True, exist_ok=False)
    sources = out / "sources"
    shutil.copytree(root / "tests/fixtures/resources_sdk", sources)
    env = dict(os.environ)
    for key in ("CPATH", "C_INCLUDE_PATH", "CPLUS_INCLUDE_PATH", "LIBRARY_PATH",
                "LD_LIBRARY_PATH", "DYLD_LIBRARY_PATH"):
        env.pop(key, None)
    cc = shlex.split(env.get("CC", "cc"))
    cxx = shlex.split(env.get("CXX", "c++"))
    prefixes = {"old": args.old.resolve(), "new": args.new.resolve()}
    calls, results = [], []

    def run(argv, name, expect_failure=False):
        command = [str(a) for a in argv]
        proc = subprocess.run(command, cwd=out, env=env, text=True,
                              stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
        (out / (name + ".log")).write_text(proc.stdout)
        calls.append({"name": name, "command": command, "exit_code": proc.returncode})
        (out / "commands.json").write_text(json.dumps(calls, indent=2) + "\n")
        if expect_failure:
            if proc.returncode == 0 or "maelys_datalog_session_config_set_resources" not in proc.stdout:
                raise RuntimeError("expected missing resource API at old-host link: " + name)
        elif proc.returncode:
            raise RuntimeError(name + ": " + proc.stdout[-4000:])
        return proc.stdout

    def compile_object(source, sdk, name, tail=False):
        obj = out / (name + ".o")
        run(cc + ["-std=c11", "-O2", "-UNDEBUG", "-pedantic-errors", "-Wall", "-Wextra", "-Werror",
                  "-I" + str(prefixes[sdk] / "include")] +
            (["-include", str(prefixes[sdk] / "include/maelys/datalog_advanced.h")]
             if (prefixes[sdk] / "include/maelys/datalog_advanced.h").exists() else []) +
            (["-DOPTIONAL_TAIL"] if tail else []) +
            ["-c", sources / source, "-o", obj], name + "-compile")
        return obj

    def library(sdk, linkage):
        lib = prefixes[sdk] / "lib"
        if not lib.exists():
            lib = prefixes[sdk] / "lib64"
        name = "libmaelys_datalog.a" if linkage == "static" else (
            "libmaelys_datalog_shared.dylib" if sys.platform == "darwin" else "libmaelys_datalog_shared.so")
        return lib / name

    def link_run(objects, sdk, linkage, name, fail=False):
        lib = library(sdk, linkage)
        run(cc + [*objects, lib, "-Wl,-rpath," + str(lib.parent), "-o", out / name],
            name + "-link", expect_failure=fail)
        if fail:
            results.append({"case": name, "result": "expected undefined new API at link"})
            return None
        text = run([out / name], name)
        results.append({"case": name, "result": "PASS"})
        return text

    run(cc + ["--version"], "compiler")
    callers = {v: compile_object("caller5.c", v, "caller5-" + v) for v in prefixes}
    providers = {v: compile_object("provider5.c", v, "provider5-" + v) for v in prefixes}
    baseline = None
    for host in prefixes:
        for linkage in ("static", "shared"):
            for caller in prefixes:
                for provider in prefixes:
                    name = f"v5-{caller}-caller-{provider}-provider-{host}-host-{linkage}"
                    text = link_run([callers[caller], providers[provider]], host, linkage, name)
                    if baseline is None:
                        baseline = text
                    if text != baseline:
                        raise RuntimeError("ABI5 fingerprint/bounds/behavior changed: " + name)
    for caller_tail in (False, True):
        caller = compile_object("caller6.c", "new", f"caller6-tail{int(caller_tail)}", caller_tail)
        for provider_tail in (False, True):
            provider = compile_object("provider6.c", "new", f"provider6-tail{int(provider_tail)}", provider_tail)
            for old_provider in prefixes:
                for linkage in ("static", "shared"):
                    name = f"v6-c{int(caller_tail)}-p{int(provider_tail)}-abi5{old_provider}-{linkage}"
                    link_run([caller, provider, providers[old_provider]], "new", linkage, name)
            if not caller_tail and not provider_tail:
                for linkage in ("static", "shared"):
                    link_run([caller, provider, providers["old"]], "old", linkage,
                             "new-api-old-host-" + linkage, fail=True)
    layout = None
    for profile in ("SMALL", "LARGE", "XLARGE_BOUNDARY"):
        # XLARGE_BOUNDARY has no engine/profile implementation: it is only an
        # extra consumer compilation label proving public record independence.
        for language, compiler, std in (("c", cc, "c11"), ("c++", cxx, "c++17")):
            name = f"layout-{profile}-{std}"
            run(compiler + ["-x", language, "-std=" + std, "-pedantic-errors", "-Wall", "-Wextra", "-Werror",
                           "-DMAELYS_DATALOG_PROFILE_" + profile,
                           "-I" + str(prefixes["new"] / "include"), sources / "layout.c", "-o", out / name], name + "-compile")
            text = run([out / name], name)
            if layout is None:
                layout = text
            if text != layout:
                raise RuntimeError("public resource layout depends on profile/language: " + name)
            results.append({"case": name, "result": "PASS"})
    paths = sorted(sources.glob("*")) + sorted(out.glob("*.o"))
    paths += [library(sdk, link) for sdk in prefixes for link in ("static", "shared")]
    hashes = {str(p): hashlib.sha256(p.read_bytes()).hexdigest() for p in paths}
    report = {"schema": 1, "prefixes": {k: str(v) for k, v in prefixes.items()},
              "results": results, "sha256": hashes,
              "limits": "Synthetic XLARGE transport only; no XLARGE engine or private ABI6 provider; no timing claim."}
    (out / "report.json").write_text(json.dumps(report, indent=2) + "\n")
    print(f"{len(results)} separate-build cases PASS; evidence: {out / 'report.json'}")


if __name__ == "__main__":
    main()
