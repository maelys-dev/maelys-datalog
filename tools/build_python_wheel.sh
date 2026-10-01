#!/usr/bin/env bash
# SPDX-License-Identifier: MPL-2.0
# One wheel, two native profiles, built and installed before release receipts.
set -euo pipefail
root="$(cd "$(dirname "$0")/.." && pwd)"
target="${1:?native release target required}"
if [ "$target" = _manylinux ]; then
  out="${3:?wheel output directory required}"
else
  out="${2:?wheel output directory required}"
fi
mkdir -p "$out"
out="$(cd "$out" && pwd)"
case "$target" in
  linux-x86_64|linux-arm64)
    image="$(python3 -c 'import json,sys; print(json.load(open(sys.argv[1]))["images"][sys.argv[2]])' "$root/tools/python-wheel-build.json" "$target")"
    if [ "$target" = linux-x86_64 ]; then arch=amd64; else arch=arm64; fi
    # This path is used by both PR packaging and the unchanged generated socle.
    # The build image is immutable; no host SDK or native objects enter it.
    docker run --rm --platform "linux/$arch" --user "$(id -u):$(id -g)" \
      -v "$root:/src:ro" -v "$out:/out" -e PIP_CACHE_DIR=/tmp/wheel-pip-cache \
      "$image" bash /src/tools/build_python_wheel.sh _manylinux "$target" /out
    exit
    ;;
  _manylinux)
    target="$2"; out="$3"
    python=/opt/python/cp310-cp310/bin/python
    ;;
  macos-arm64)
    python="${PYTHON:-python3}"
    ;;
  *) echo "unsupported wheel target: $target" >&2; exit 2 ;;
esac
scratch="$(mktemp -d)"
trap 'rm -rf -- "$scratch"' EXIT
"$python" -m venv "$scratch/venv"
python="$scratch/venv/bin/python"
"$python" - "$root/tools/python-wheel-build.json" > "$scratch/requirements.txt" <<'PY'
import json,platform,sys
p=json.load(open(sys.argv[1])); print('\n'.join(p['requirements']))
if platform.system()=='Linux': print(p['auditwheel'])
PY
"$python" -m pip install --disable-pip-version-check -r "$scratch/requirements.txt"
"$python" "$root/tools/build_python_wheel.py" "$target" "$out"
