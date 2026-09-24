#!/usr/bin/env python3
# SPDX-License-Identifier: MPL-2.0
"""Compare independent native Debug builds, including their final SDK tarballs."""
import argparse
import hashlib
import os
from pathlib import Path
import shutil
import subprocess
import tempfile
import tarfile


def run(*args, **kwargs):
    subprocess.run(args, check=True, **kwargs)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("profile", choices=("SMALL", "LARGE"))
    args = parser.parse_args()
    root = Path(__file__).resolve().parent.parent
    paths = subprocess.check_output(["git", "ls-files", "-z", "--cached", "--others", "--exclude-standard"], cwd=root).split(b"\0")
    epoch = subprocess.check_output(["git", "log", "-1", "--format=%ct"], cwd=root).decode().strip()
    env = dict(os.environ, SOURCE_DATE_EPOCH=epoch)
    with tempfile.TemporaryDirectory(prefix="maelys-repro-") as tmp:
        stage = Path(tmp).resolve()
        archives, libraries = [], []
        for name in ("first", "different-length-second"):
            source = stage / name / "source"
            build = stage / name / "build"
            for raw in paths:
                if not raw:
                    continue
                relative = Path(os.fsdecode(raw))
                target = source / relative
                target.parent.mkdir(parents=True, exist_ok=True)
                shutil.copy2(root / relative, target)
                # Different checkout mtimes must not influence the tarball.
                os.utime(target, (int(epoch) + len(name),) * 2)
            run("cmake", "-S", str(source), "-B", str(build), "-DBUILD_TESTING=OFF",
                "-DCMAKE_C_COMPILER=" + os.environ.get("CC", "clang"),
                "-DCMAKE_BUILD_TYPE=Debug", "-DCMAKE_C_FLAGS_DEBUG=-g",
                "-DMAELYS_DATALOG_REPRODUCIBLE_PATHS=ON",
                "-DCMAKE_INSTALL_LIBDIR=lib", "-DCMAKE_INSTALL_INCLUDEDIR=include",
                "-DMAELYS_DATALOG_PROFILE_LARGE=" + ("ON" if args.profile == "LARGE" else "OFF"), env=env)
            run("cmake", "--build", str(build), "--target", "maelys_datalog", "--parallel", "4", env=env)
            archive = stage / (name + ".tar.gz")
            run("bash", str(source / "scripts/package-native-sdk.sh"), str(build), str(archive), env=env)
            with tarfile.open(archive) as tar:
                library = tar.extractfile("./lib/libmaelys_datalog.a").read()
            if b"/maelys-datalog-build" not in library or str(stage).encode() in library:
                raise SystemExit("DWARF canonical build path missing or temporary path leaked")
            libraries.append(library)
            archives.append(archive.read_bytes())
        if libraries[0] != libraries[1]:
            raise SystemExit("Independent native libraries differ")
        if archives[0] != archives[1]:
            raise SystemExit("Independent native SDK archives differ")
        print(f"PASS {args.profile}: independent Debug builds and SDK archives identical; SHA256 {hashlib.sha256(archives[0]).hexdigest()}")


if __name__ == "__main__":
    main()
