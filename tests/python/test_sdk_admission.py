"""A matching installation builds in the outer gate; incompatible API fails.

Keep the current layouts and change only the declared version, so this test
fails if the explicit API admission guard is removed (rather than succeeding
because a different historical header happened to miss unrelated fields).
"""
from pathlib import Path
import os
import shutil
import subprocess
import sys

import pytest


@pytest.mark.parametrize("version", [1, 3])
def test_builder_rejects_incompatible_consumer_api(tmp_path, version):
    sdk = tmp_path / "sdk"
    shutil.copytree(Path(os.environ["MAELYS_DATALOG_SDK_PREFIX"]), sdk)
    header = sdk / "include/maelys/datalog.h"
    contents = header.read_text()
    assert contents.count("#define MAELYS_DATALOG_PUBLIC_API_VERSION 2u") == 1
    header.write_text(contents.replace("#define MAELYS_DATALOG_PUBLIC_API_VERSION 2u",
                                      f"#define MAELYS_DATALOG_PUBLIC_API_VERSION {version}u"))
    # Each negative control has a fresh source tree and no cached extension.
    consumer = tmp_path / "consumer"
    source = Path(__file__).resolve().parents[2] / "bindings/python"
    shutil.copytree(source, consumer,
                    ignore=shutil.ignore_patterns("build", "__pycache__", "*.so", "*.dylib"))
    environment = os.environ.copy()
    for name in ("CPATH", "C_INCLUDE_PATH", "CPLUS_INCLUDE_PATH", "LIBRARY_PATH"):
        environment.pop(name, None)
    completed = subprocess.run([sys.executable, str(consumer / "build_cffi.py"),
                                "--sdk-prefix", str(sdk)], cwd=consumer,
                               capture_output=True, text=True, env=environment)
    output = completed.stdout + completed.stderr
    assert completed.returncode != 0, output
    assert "Consumer API 2 required" in output
    assert not list((consumer / "maelys_datalog").glob("_maelys_cffi*.so"))
