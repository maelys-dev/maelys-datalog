#!/usr/bin/env python3
"""Inspect and install the actual wheel outside the checkout, then exercise it."""
from __future__ import annotations

import argparse
import json
import os
from pathlib import Path, PurePosixPath
import platform
import re
import shutil
import subprocess
import sys
import tempfile
from email.parser import BytesParser

from packaging.utils import parse_wheel_filename
from wheel.wheelfile import WheelFile

ROOT = Path(__file__).resolve().parents[1]
POLICY = json.loads((ROOT / "tools/python-wheel-build.json").read_text())


def run(*args, **kwargs):
    return subprocess.run(list(map(str, args)), check=True, **kwargs)


def inspect_archive(wheel: Path, dest: Path) -> list[str]:
    name, version, build, tags = parse_wheel_filename(wheel.name)
    assert name == "maelys-datalog" and str(version) == (ROOT / "VERSION").read_text().strip() and not build
    assert len(tags) == 1
    tag = next(iter(tags))
    assert tag.interpreter == "cp310" and tag.abi == "abi3"
    assert tag.platform in ("manylinux_2_28_x86_64", "manylinux_2_28_aarch64", "macosx_13_0_arm64")
    info = f"maelys_datalog-{version}.dist-info"
    expected = {f"maelys_datalog/{name}" for name in
                ("engine.py", "__init__.py", "_maelys_cffi.py", "_wheel_profile.py")}
    expected |= {f"maelys_datalog/_{p}/{name}" for p in ("small", "large")
                 for name in ("__init__.py", "_maelys_cffi.abi3.so")}
    expected |= {f"{info}/{name}" for name in
                 ("METADATA", "WHEEL", "RECORD", "top_level.txt", "licenses/LICENSE", "licenses/yyjson/LICENSE")}
    with WheelFile(wheel) as archive:
        names = archive.namelist()
        assert len(names) == len(set(names)) and set(names) == expected, "unexpected or missing payload"
        for name in names:
            path = PurePosixPath(name)
            assert not path.is_absolute() and ".." not in path.parts
            assert not path.suffix in (".pth", ".pyc", ".a", ".h")
            data = archive.read(name)  # WheelFile verifies every RECORD digest.
            path = dest.joinpath(*path.parts)
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_bytes(data)
        extensions = [n for n in names if n.endswith(".so")]
        assert set(extensions) == {f"maelys_datalog/_{p}/_maelys_cffi.abi3.so" for p in ("small", "large")}
        for name in ("engine.py", "__init__.py"):
            assert archive.read("maelys_datalog/" + name) == (ROOT / "bindings/python/maelys_datalog" / name).read_bytes()
        metadata = BytesParser().parsebytes(archive.read(info + "/METADATA"))
        assert metadata["Name"] == "maelys-datalog" and metadata["Version"] == str(version)
        assert metadata["Requires-Python"] == ">=3.10,<3.15"
        assert metadata.get_all("Requires-Dist") == ["cffi>=2.1.1,<3"]
        wheel_metadata = BytesParser().parsebytes(archive.read(info + "/WHEEL"))
        assert wheel_metadata["Root-Is-Purelib"] == "false"
        assert wheel_metadata.get_all("Tag") == [str(tag)]
    return extensions


def inspect(wheel: Path, dest: Path) -> None:
    extensions = inspect_archive(wheel, dest)
    for relative in extensions:
        binary = dest / relative
        if platform.system() == "Darwin":
            symbols = set(subprocess.check_output(["nm", "-gUj", binary], text=True).split())
            assert symbols == {"_PyInit__maelys_cffi"}, symbols
            commands = subprocess.check_output(["otool", "-l", binary], text=True)
            assert re.findall(r"^\s*minos ([0-9.]+)$", commands, re.M) == [POLICY["macosMinimum"]]
            assert "__debug_" not in commands
            deps = subprocess.check_output(["otool", "-L", binary], text=True).splitlines()[1:]
            assert all(line.strip().startswith(("/usr/lib/", "/System/Library/")) for line in deps), deps
        else:
            symbols = {line.split()[-1] for line in subprocess.check_output(["nm", "-D", "--defined-only", binary], text=True).splitlines()}
            assert symbols == {"PyInit__maelys_cffi"}, symbols
            sections = subprocess.check_output(["readelf", "-SW", binary], text=True)
            assert ".symtab" not in sections and ".debug_" not in sections
            versions = re.findall(r"Name: (\S+)", subprocess.check_output(["readelf", "--version-info", binary], text=True))
            assert versions and all(re.fullmatch(r"GLIBC_[0-9.]+", v) for v in versions), versions
            assert max(tuple(map(int, v.removeprefix("GLIBC_").split('.'))) for v in versions) <= (2, 28)
            deps = re.findall(r"Shared library: \[(.*?)\]", subprocess.check_output(["readelf", "-d", binary], text=True))
            assert set(deps) <= {"libc.so.6", "libm.so.6", "libdl.so.2", "libpthread.so.0", "librt.so.1", "ld-linux-x86-64.so.2", "ld-linux-aarch64.so.1"}, deps
    run(Path(sys.executable).with_name("abi3audit"), "--strict", "--summary", wheel)
    if platform.system() == "Linux":
        run(Path(sys.executable).with_name("auditwheel"), "show", wheel)


def check(wheel: Path, sdks: dict[str, Path | None]) -> None:
    with tempfile.TemporaryDirectory(prefix="maelys-wheel-consumer-") as temp:
        work = Path(temp)
        inspect(wheel, work / "inspection")
        run(sys.executable, "-m", "venv", work / "venv")
        python = work / "venv/bin/python"
        env = os.environ.copy()
        for key in ("PYTHONPATH", "CPATH", "C_INCLUDE_PATH", "CPLUS_INCLUDE_PATH", "LIBRARY_PATH", "LD_LIBRARY_PATH", "DYLD_LIBRARY_PATH", "MAELYS_DATALOG_PROFILE"):
            env.pop(key, None)
        env["PYTHONNOUSERSITE"] = "1"
        # Install only binary dependencies. Neither installation nor import can
        # invoke a compiler or discover an SDK from the caller's environment.
        env.update(CC="/no/compiler", CXX="/no/compiler")
        run(python, "-m", "pip", "install", "--disable-pip-version-check", "--only-binary=:all:", wheel, "pytest==9.1.1", env=env, cwd=work)
        if any(sdks.values()):
            # CFFI's negative SDK compilation controls need setuptools on
            # Python >=3.12. This is a build-test dependency, not a dependency
            # of the precompiled wheel or its compiler-free consumption.
            setuptools = next(r for r in POLICY["requirements"] if r.startswith("setuptools=="))
            run(python, "-m", "pip", "install", "--only-binary=:all:", setuptools, env=env, cwd=work)
        consumer = work / "consumer"
        shutil.copytree(ROOT / "tests/python", consumer / "tests/python", ignore=shutil.ignore_patterns("__pycache__", ".pytest_cache"))
        shutil.copytree(ROOT / "bindings/python", consumer / "bindings/python",
                        ignore=shutil.ignore_patterns("build", "__pycache__", "*.so", "*.dylib"))
        # Source bridge checks and header coverage remain separate from runtime
        # import: the package must come exclusively from the fresh venv.
        header_sdk = consumer / "header-contract"
        shutil.copytree(ROOT / "include/maelys", header_sdk / "include/maelys")
        for profile in ("small", "large"):
            profile_env = {**env, "MAELYS_DATALOG_PROFILE": profile, "MAELYS_DATALOG_EXPECT_PROFILE": profile,
                           "MAELYS_DATALOG_SDK_PREFIX": str(sdks[profile] or header_sdk)}
            run(python, "-c", "import pathlib,sys,maelys_datalog; p=pathlib.Path(maelys_datalog.__file__).resolve(); assert p.is_relative_to(pathlib.Path(sys.prefix)); print('installed wheel:',p)", env=profile_env, cwd=work)
            command = [python, "-m", "pytest", "-q", "tests/python"]
            if sdks[profile] is None:
                # The build gate runs these two negative compilation controls
                # against each installed SDK; consuming a wheel needs no SDK.
                command += ["--ignore=tests/python/test_sdk_admission.py"]
            else:
                profile_env.pop("CC", None); profile_env.pop("CXX", None)
            run(*command, cwd=consumer, env=profile_env)
            other = "large" if profile == "small" else "small"
            code = "import importlib,os,maelys_datalog; os.environ['MAELYS_DATALOG_PROFILE']='" + other + "'; importlib.import_module('maelys_datalog._" + other + "._maelys_cffi')"
            result = subprocess.run([str(python), "-c", code], cwd=work, env=profile_env, text=True, capture_output=True)
            assert result.returncode and "profile is fixed" in result.stderr, result.stderr
        bad = subprocess.run([str(python), "-c", "import maelys_datalog"], cwd=work,
                             env={**env, "MAELYS_DATALOG_PROFILE": "unknown"}, capture_output=True, text=True)
        assert bad.returncode and "must be 'small' or 'large'" in bad.stderr
        run(python, "-m", "pip", "check", env=env, cwd=work)
    print(f"{wheel.name}: metadata, RECORD, abi3, native policy, fresh pip install and both profiles PASS")


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("wheel", type=Path)
    parser.add_argument("--small-sdk", type=Path)
    parser.add_argument("--large-sdk", type=Path)
    args = parser.parse_args()
    check(args.wheel.resolve(), {"small": args.small_sdk, "large": args.large_sdk})
