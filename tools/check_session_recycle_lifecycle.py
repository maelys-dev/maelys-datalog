#!/usr/bin/env python3
# SPDX-License-Identifier: MPL-2.0
"""Build an instrumented DSO; observe its last retained block at real dlclose."""
import argparse
import pathlib
import subprocess
import sys


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument("--compiler", default="clang")
    p.add_argument("--large", action="store_true")
    p.add_argument("--output", type=pathlib.Path, required=True)
    p.add_argument("--overlay", type=pathlib.Path, help="private mutation include overlay; never production sources")
    args = p.parse_args()
    root = pathlib.Path(__file__).resolve().parents[1]
    out = args.output.resolve()
    out.mkdir(parents=True, exist_ok=True)
    sources = []
    for name in ("core", "native", "standard"):
        sources += [str(root / s.strip()) for s in (root / f"build-support/{name}-sources.txt").read_text().splitlines()
                    if s.strip() and not s.startswith("#")]
    flags = ["-std=c11", "-D_POSIX_C_SOURCE=200809L", "-Wall", "-Wextra", "-Werror", "-O2", "-UNDEBUG",
             "-I" + str(root), "-I" + str(root / "include")]
    if args.overlay:
        flags.insert(0, "-I" + str(args.overlay.resolve()))
    if args.large:
        flags += ["-DMAELYS_DATALOG_PROFILE_LARGE"]
    plugin = out / ("recycle.dylib" if sys.platform == "darwin" else "recycle.so")
    subprocess.run([args.compiler, *flags, "-fPIC", "-shared", "-include",
                    str(root / "tests/fixtures/allocation_guard.h"), *sources,
                    str(root / "tests/fixtures/session_recycle_plugin.c"), "-o", str(plugin)], check=True)
    loader = out / "loader"
    subprocess.run([args.compiler, *flags, str(root / "tests/fixtures/session_recycle_loader.c"),
                    *([] if sys.platform == "darwin" else ["-ldl"]), "-o", str(loader)], check=True)
    subprocess.run([str(loader), str(plugin)], check=True)


if __name__ == "__main__":
    main()
