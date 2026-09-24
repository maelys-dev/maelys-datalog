#!/usr/bin/env python3
# SPDX-License-Identifier: MPL-2.0
"""Package native SDK files with deterministic tar/gzip metadata, no host xattrs."""
import gzip
from pathlib import Path
import sys
import tarfile


def package(prefix, output, epoch):
    if not 0 <= epoch <= 0xFFFFFFFF:
        raise ValueError("SOURCE_DATE_EPOCH must be an integer in [0, 4294967295]")
    with output.open("wb") as raw:
        with gzip.GzipFile(filename="", mode="wb", fileobj=raw, mtime=epoch) as gz:
            with tarfile.open(fileobj=gz, mode="w", format=tarfile.PAX_FORMAT) as tar:
                for path in [prefix, *sorted(prefix.rglob("*"))]:
                    if path.is_symlink() or not (path.is_dir() or path.is_file()):
                        raise ValueError(f"Unexpected SDK member: {path}")
                    name = "." if path == prefix else "./" + path.relative_to(prefix).as_posix()
                    info = tar.gettarinfo(str(path), arcname=name)
                    info.uid = info.gid = 0
                    info.uname = info.gname = ""
                    info.mtime = epoch
                    info.mode = 0o755 if path.is_dir() or info.mode & 0o111 else 0o644
                    info.pax_headers = {}
                    if path.is_file():
                        with path.open("rb") as contents:
                            tar.addfile(info, contents)
                    else:
                        tar.addfile(info)


if __name__ == "__main__":
    if len(sys.argv) != 4:
        raise SystemExit("usage: package_deterministic_tar.py PREFIX OUTPUT EPOCH")
    package(Path(sys.argv[1]), Path(sys.argv[2]), int(sys.argv[3]))
