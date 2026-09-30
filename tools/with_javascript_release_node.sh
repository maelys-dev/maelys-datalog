#!/usr/bin/env bash
# SPDX-License-Identifier: MPL-2.0
# The release must not inherit an unrelated Node from the runner's PATH.
set -euo pipefail
root="$(cd "$(dirname "$0")/.." && pwd)"
version="$(python3 -c 'import json,sys; print(json.load(open(sys.argv[1]))["nodeVersion"])' "$root/tools/javascript-build.json")"
case "$(uname -s)-$(uname -m)" in
  Linux-x86_64) platform=linux-x64 ;;
  Linux-aarch64) platform=linux-arm64 ;;
  Darwin-arm64) platform=darwin-arm64 ;;
  *) echo 'Unsupported JavaScript release build host' >&2; exit 1 ;;
esac
scratch="$(mktemp -d)"
trap 'rm -rf -- "$scratch"' EXIT
name="node-v$version-$platform.tar.gz"
curl --fail --silent --show-error --location "https://nodejs.org/dist/v$version/$name" -o "$scratch/$name"
python3 - "$scratch" "$name" "$root/tools/javascript-node-sha256.txt" <<'PY'
import hashlib, pathlib, sys, tarfile
scratch, name, sums = sys.argv[1:]
archive = pathlib.Path(scratch) / name
expected = [line.split()[0] for line in pathlib.Path(sums).read_text().splitlines() if line.split()[-1] == name]
assert len(expected) == 1 and hashlib.sha256(archive.read_bytes()).hexdigest() == expected[0], 'Node distribution checksum mismatch'
with tarfile.open(archive) as stream:
    stream.extractall(scratch, filter='data')
PY
export PATH="$scratch/node-v$version-$platform/bin:$PATH"
export NODE_INCLUDE_DIR="$scratch/node-v$version-$platform/include/node"
test "$(node -p process.versions.node)" = "$version"
echo "JavaScript release build/test runtime: Node $(node --version) ($platform)"
"$@"
