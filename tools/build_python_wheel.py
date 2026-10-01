#!/usr/bin/env python3
"""Build one precompiled wheel from fresh installed SMALL and LARGE SDKs."""
from __future__ import annotations

import argparse
import importlib.util
import json
import os
from pathlib import Path
import platform
import shutil
import subprocess
import sys
import tempfile

from wheel.wheelfile import WheelFile

ROOT = Path(__file__).resolve().parents[1]
POLICY = json.loads((ROOT / "tools/python-wheel-build.json").read_text())
PLATFORMS = {"linux-x86_64": "manylinux_2_28_x86_64",
             "linux-arm64": "manylinux_2_28_aarch64",
             "macos-arm64": "macosx_13_0_arm64"}


def run(*args, **kwargs):
    subprocess.run(list(map(str, args)), check=True, **kwargs)


def build(target: str, output: Path) -> Path:
    expected = {"linux-x86_64": ("Linux", "x86_64"), "linux-arm64": ("Linux", "aarch64"),
                "macos-arm64": ("Darwin", "arm64")}[target]
    if (platform.system(), platform.machine()) != expected:
        raise SystemExit(f"{target}: native build required, got {platform.system()}/{platform.machine()}")
    if sys.implementation.name != "cpython" or not (3, 10) <= sys.version_info[:2] < (3, 15):
        raise SystemExit("wheel build requires CPython 3.10–3.14")
    version = (ROOT / "VERSION").read_text().strip()
    output.mkdir(parents=True, exist_ok=True)
    tag = "cp310-abi3-" + PLATFORMS[target]
    filename = f"maelys_datalog-{version}-{tag}.whl"
    if (output / filename).exists():
        raise SystemExit(f"refusing to overwrite {output / filename}")
    spec = importlib.util.spec_from_file_location("wheel_cffi_builder", ROOT / "bindings/python/build_cffi.py")
    builder = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(builder)
    env = os.environ.copy()
    for key in ("CPATH", "C_INCLUDE_PATH", "CPLUS_INCLUDE_PATH", "LIBRARY_PATH", "PYTHONPATH", "CFLAGS", "LDFLAGS"):
        env.pop(key, None)
    if target == "macos-arm64":
        env["MACOSX_DEPLOYMENT_TARGET"] = POLICY["macosMinimum"]
    # Compile all native code before the archive installation checks.
    with tempfile.TemporaryDirectory(prefix="maelys-python-wheel-") as temp:
        work = Path(temp)
        stage = work / "stage"
        package = stage / "maelys_datalog"
        package.mkdir(parents=True)
        for name in ("__init__.py", "engine.py"):
            shutil.copyfile(ROOT / "bindings/python/maelys_datalog" / name, package / name)
        shutil.copyfile(ROOT / "bindings/python/wheel_loader.py", package / "_maelys_cffi.py")
        shutil.copyfile(ROOT / "bindings/python/wheel_profile.py", package / "_wheel_profile.py")
        sdks = {}
        for profile in ("small", "large"):
            native = work / profile
            sdk = work / (profile + "-sdk")
            sdks[profile] = sdk
            run("cmake", "-S", ROOT, "-B", native, "-DBUILD_TESTING=OFF", "-DCMAKE_BUILD_TYPE=Release",
                "-DCMAKE_INSTALL_LIBDIR=lib", "-DCMAKE_INSTALL_INCLUDEDIR=include",
                "-DCMAKE_POSITION_INDEPENDENT_CODE=ON", "-DCMAKE_C_VISIBILITY_PRESET=hidden",
                "-DMAELYS_DATALOG_REPRODUCIBLE_PATHS=ON",
                "-DMAELYS_DATALOG_PROFILE_LARGE=" + ("ON" if profile == "large" else "OFF"), env=env)
            run("cmake", "--build", native, "--target", "maelys_datalog", "maelys_datalog_shared", "--parallel", "2", env=env)
            run("cmake", "--install", native, "--prefix", sdk, env=env)
            destination = package / ("_" + profile)
            destination.mkdir()
            (destination / "__init__.py").write_text(
                'from .._wheel_profile import PROFILE\n'
                f'if PROFILE != "{profile}":\n'
                '    raise ImportError("the wheel profile is fixed before the first import")\n')
            # Same checked CFFI declarations, linked to installed PIC archive.
            previous = os.environ.copy()
            try:
                os.environ.clear(); os.environ.update(env)
                builder.build(sdk, package=destination, module=f"maelys_datalog._{profile}._maelys_cffi",
                              static_abi3=True, work=work / (profile + "-cffi"))
            finally:
                os.environ.clear(); os.environ.update(previous)
            extension = destination / "_maelys_cffi.abi3.so"
            run("strip", *( ["-S", "-x"] if target == "macos-arm64" else ["--strip-unneeded"]), extension)
        info = stage / f"maelys_datalog-{version}.dist-info"
        (info / "licenses/yyjson").mkdir(parents=True)
        shutil.copyfile(ROOT / "LICENSE", info / "licenses/LICENSE")
        shutil.copyfile(ROOT / "vendor/yyjson/LICENSE", info / "licenses/yyjson/LICENSE")
        (info / "METADATA").write_text(
            "Metadata-Version: 2.4\nName: maelys-datalog\n" + f"Version: {version}\n"
            "Summary: Maelys Datalog Python binding with bundled native SMALL and LARGE engines\n"
            "Requires-Python: >=3.10,<3.15\nRequires-Dist: cffi>=2.1.1,<3\n"
            "License-Expression: MPL-2.0 AND MIT\nLicense-File: LICENSE\n"
            "License-File: yyjson/LICENSE\n"
            "Project-URL: Source, https://github.com/maelys-dev/maelys-datalog\n"
            "Description-Content-Type: text/markdown\n\n" + (ROOT / "bindings/python/README.md").read_text())
        (info / "WHEEL").write_text(f"Wheel-Version: 1.0\nGenerator: maelys-release-wheel\nRoot-Is-Purelib: false\nTag: {tag}\n")
        (info / "top_level.txt").write_text("maelys_datalog\n")
        candidate = work / filename
        with WheelFile(candidate, "w") as archive:
            archive.write_files(stage)
        run(sys.executable, ROOT / "tools/check_python_wheel.py", candidate, "--small-sdk", sdks["small"], "--large-sdk", sdks["large"], env=env)
        shutil.copyfile(candidate, output / filename)
    print(output / filename)
    return output / filename


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("target", choices=PLATFORMS)
    parser.add_argument("output", type=Path)
    args = parser.parse_args()
    build(args.target, args.output.resolve())
