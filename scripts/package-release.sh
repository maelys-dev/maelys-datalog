#!/usr/bin/env bash
#
# Build and package the release artifacts for maelys-datalog:
#   - native : lib/libmaelys_datalog.a + public headers, one tarball per
#              supported (os, arch) target
#   - wasm   : maelys_datalog_dynamic.{js,wasm} + the JS wrapper/types, one
#              tarball per memory profile (small, large)
#
# One command, used both locally and by .github/workflows/release.yml (see
# docs/release-engineering.md). Outputs to dist/; everything else happens in
# disposable staging directories that are removed before the script exits.
#
# Usage:
#   scripts/package-release.sh [target-label] [--wasm] [--wasm-only]
#   scripts/package-release.sh --merge-receipts DIR
#
#   target-label   defaults to <os>-<arch> auto-detected from uname
#                  (linux-x86_64, linux-arm64, macos-arm64 — the only
#                  targets in docs/release-engineering.md D1). Any other
#                  OS/arch, detected or explicit, is refused.
#   --wasm         also build the WASM artifacts alongside the native one.
#                  Requires emcc pinned to exactly $EMSDK_VERSION; fails
#                  otherwise (no silent fallback to whatever is on PATH).
#   --wasm-only    build only the WASM artifacts (skip the native one). Used
#                  by the dedicated WASM job in release.yml. Same pin
#                  enforcement as --wasm.
#   --merge-receipts DIR
#                  merge every release-receipt*.json found under DIR into a
#                  single dist/release-receipt.json (same version/commit
#                  required, union of artifacts/channels). Used by the
#                  publish job, which never compiles anything itself. All
#                  other flags are ignored/rejected in this mode.
#
# Auto-detection note (default invocation, no --wasm/--wasm-only): the WASM
# stage is attempted only when emcc is on PATH *and* already reports the
# pinned $EMSDK_VERSION. A stray/mismatched emcc on PATH is treated as "not
# available" and silently skipped in this mode — the script never builds
# with an unpinned tool, but it also never lets an incidental toolchain on a
# dev machine block the native artifact. Once a WASM build is explicitly
# requested (--wasm / --wasm-only), the pin is mandatory: a mismatch is a
# hard failure per docs/release-engineering.md D2.
set -euo pipefail

# Pinned emsdk version (D2). Update deliberately, in a dedicated PR, the same
# way action SHAs are bumped — never silently.
EMSDK_VERSION="3.1.61"

KNOWN_TARGETS="linux-x86_64 linux-arm64 macos-arm64"

root="$(cd "$(dirname "$0")/.." && pwd)"
cd "$root"

# Portable SHA-256 (Linux: sha256sum, macOS: shasum -a 256).
sha256() {
  if command -v sha256sum >/dev/null 2>&1; then sha256sum "$@"; else shasum -a 256 "$@"; fi
}

usage() {
  cat >&2 <<'USAGE'
Usage: scripts/package-release.sh [target-label] [--wasm] [--wasm-only]
       scripts/package-release.sh --merge-receipts DIR
USAGE
}

target=""
wasm_requested=0
wasm_only=0
merge_dir=""

while [ $# -gt 0 ]; do
  case "$1" in
    --wasm)
      wasm_requested=1
      shift
      ;;
    --wasm-only)
      wasm_requested=1
      wasm_only=1
      shift
      ;;
    --merge-receipts)
      if [ -z "${2:-}" ]; then
        echo "error: --merge-receipts requires a directory argument" >&2
        exit 1
      fi
      merge_dir="$2"
      shift 2
      ;;
    -h|--help)
      usage
      exit 0
      ;;
    -*)
      echo "error: unknown flag: $1" >&2
      usage
      exit 1
      ;;
    *)
      if [ -n "$target" ]; then
        echo "error: unexpected argument: $1" >&2
        usage
        exit 1
      fi
      target="$1"
      shift
      ;;
  esac
done

version="$(cat VERSION)"
dist="$root/dist"
mkdir -p "$dist"

# ---------------------------------------------------------------------------
# --merge-receipts mode: no compilation, just fold partial receipts into one.
# ---------------------------------------------------------------------------
if [ -n "$merge_dir" ]; then
  if [ "$wasm_requested" = 1 ] || [ -n "$target" ]; then
    echo "error: --merge-receipts cannot be combined with a target or --wasm/--wasm-only" >&2
    exit 1
  fi
  if [ ! -d "$merge_dir" ]; then
    echo "error: --merge-receipts directory not found: $merge_dir" >&2
    exit 1
  fi
  if ! command -v jq >/dev/null 2>&1; then
    echo "error: jq is required for --merge-receipts" >&2
    exit 1
  fi

  files=()
  while IFS= read -r -d '' f; do files+=("$f"); done < <(find "$merge_dir" -type f -name '*.json' -print0 | sort -z)
  if [ "${#files[@]}" -eq 0 ]; then
    echo "error: no receipt JSON files found under $merge_dir" >&2
    exit 1
  fi

  jq -s '
    (.[0].version) as $v
    | (.[0].commit) as $c
    | if any(.[]; .version != $v) then error("version mismatch across receipts being merged") else . end
    | if any(.[]; .commit != $c) then error("commit mismatch across receipts being merged") else . end
    | {
        name: .[0].name,
        version: $v,
        tag: .[0].tag,
        commit: $c,
        date: .[0].date,
        emsdk_version: (
          [ .[] | select(.emsdk_version != null) | .emsdk_version ] | unique
          | if length > 1 then error("emsdk_version mismatch across receipts being merged")
            elif length == 1 then .[0]
            else null
            end
        ),
        artifacts: ([ .[] | .artifacts[] ] | unique_by(.file) | sort_by(.file)),
        channels: (reduce .[] as $r ({}; . * ($r.channels // {})))
      }
  ' "${files[@]}" > "$dist/release-receipt.json"

  echo "merged ${#files[@]} receipt(s) from $merge_dir into $dist/release-receipt.json"
  cat "$dist/release-receipt.json"
  exit 0
fi

# ---------------------------------------------------------------------------
# Target detection / validation (D1: only these three are shipped).
# ---------------------------------------------------------------------------
if [ -z "$target" ]; then
  case "$(uname -s)" in
    Linux)  os_label=linux ;;
    Darwin) os_label=macos ;;
    *) echo "error: unsupported OS: $(uname -s)" >&2; exit 1 ;;
  esac
  case "$(uname -m)" in
    x86_64|amd64)  arch_label=x86_64 ;;
    arm64|aarch64) arch_label=arm64 ;;
    *) echo "error: unsupported arch: $(uname -m)" >&2; exit 1 ;;
  esac
  target="${os_label}-${arch_label}"
fi

case " $KNOWN_TARGETS " in
  *" $target "*) : ;;
  *)
    echo "error: unsupported target: $target (expected one of: $KNOWN_TARGETS)" >&2
    exit 1
    ;;
esac

artifacts=()
emsdk_recorded="null"

# ---------------------------------------------------------------------------
# Native artifact.
# ---------------------------------------------------------------------------
if [ "$wasm_only" != 1 ]; then
  if [ ! -f include/maelys_datalog_version.h ]; then
    echo "error: include/maelys_datalog_version.h not found. Run scripts/generate-version-header.sh first." >&2
    exit 1
  fi

  echo "==> native artifact (${target})"
  make clean
  make libmaelys_datalog.a

  stage="$(mktemp -d)"
  mkdir -p "$stage/lib" "$stage/include/src/core" "$stage/include/src/manifest" "$stage/include/common"
  mkdir -p "$stage/include/maelys" "$stage/licenses/yyjson"
  cp libmaelys_datalog.a "$stage/lib/"
  cp include/maelys_datalog.h "$stage/include/"
  cp include/maelys_datalog_version.h "$stage/include/"
  cp include/maelys/datalog.h include/maelys/datalog_module.h "$stage/include/maelys/"
  # Le header public inclut les headers moteur par chemins relatifs au dépôt
  # ("src/core/...", "common/..."). Sans cette fermeture, le tarball serait
  # incompilable pour un consommateur — même arborescence que la formule brew.
  cp src/core/*.h "$stage/include/src/core/"
  cp src/manifest/*.h "$stage/include/src/manifest/"
  cp common/*.h "$stage/include/common/"
  cp LICENSE "$stage/"
  cp vendor/yyjson/LICENSE "$stage/licenses/yyjson/"
  cp CHANGELOG.md "$stage/"

  native_name="maelys-datalog-${version}-${target}.tar.gz"
  tar -czf "$dist/${native_name}" -C "$stage" .
  ( cd "$dist" && sha256 "${native_name}" > "${native_name}.sha256" )
  rm -rf "$stage"

  echo "packaged ${native_name}"
  artifacts+=("$native_name")
fi

# ---------------------------------------------------------------------------
# WASM artifacts (D1 wasm-small / wasm-large rows, D2 pinned emsdk).
# ---------------------------------------------------------------------------
do_wasm=0
if [ "$wasm_requested" = 1 ]; then
  do_wasm=1
elif command -v emcc >/dev/null 2>&1; then
  probe_ver="$(emcc --version 2>&1 | head -n1)"
  if [[ "$probe_ver" == *"$EMSDK_VERSION"* ]]; then
    do_wasm=1
  else
    echo "note: emcc found (${probe_ver}) but does not match pinned EMSDK_VERSION=${EMSDK_VERSION}; skipping WASM artifacts (pass --wasm or --wasm-only to force and get a hard failure with activation instructions)" >&2
  fi
fi

if [ "$do_wasm" = 1 ]; then
  if ! command -v emcc >/dev/null 2>&1; then
    cat >&2 <<EOF
error: emcc not found on PATH, but a WASM build was requested.
Install/activate the pinned emsdk ${EMSDK_VERSION}:
  git clone https://github.com/emscripten-core/emsdk.git
  cd emsdk
  ./emsdk install ${EMSDK_VERSION}
  ./emsdk activate ${EMSDK_VERSION}
  source ./emsdk_env.sh
EOF
    exit 1
  fi

  emcc_ver="$(emcc --version 2>&1 | head -n1)"
  if [[ "$emcc_ver" != *"$EMSDK_VERSION"* ]]; then
    cat >&2 <<EOF
error: emcc version mismatch. Found:
  ${emcc_ver}
Expected emsdk ${EMSDK_VERSION} (pinned at the top of scripts/package-release.sh).
Activate the pinned version before packaging:
  cd /path/to/emsdk
  ./emsdk install ${EMSDK_VERSION}
  ./emsdk activate ${EMSDK_VERSION}
  source ./emsdk_env.sh
No silent fallback to whatever is on PATH is permitted (docs/release-engineering.md D2).
EOF
    exit 1
  fi

  dts_path=""
  if [ -f "js/maelys_playground.d.ts" ]; then
    dts_path="js/maelys_playground.d.ts"
  else
    dts_path="$(find . -path ./build -prune -o -path ./dist -prune -o -iname 'maelys_playground.d.ts' -print 2>/dev/null | head -n1)"
  fi
  if [ -z "$dts_path" ]; then
    echo "note: maelys_playground.d.ts not found anywhere in the repo; omitting it from the wasm tarballs" >&2
  fi

  build_wasm_profile() {  # $1 = small|large, $2 = WASM_BUILD_DIR
    local profile="$1" build_dir="$2"
    echo "==> wasm profile: ${profile} (${build_dir})"
    rm -f "$build_dir/maelys_datalog_dynamic.js" "$build_dir/maelys_datalog_dynamic.wasm"
    make -f Makefile.wasm maelys_datalog_dynamic.js WASM_PROFILE="$profile" WASM_BUILD_DIR="$build_dir"
  }

  stage_wasm() {  # $1 = small|large (tarball suffix), $2 = WASM_BUILD_DIR
    local suffix="$1" build_dir="$2"
    local stage
    stage="$(mktemp -d)"
    cp "$build_dir/maelys_datalog_dynamic.js" "$stage/"
    cp "$build_dir/maelys_datalog_dynamic.wasm" "$stage/"
    cp js/maelys_playground.js "$stage/"
    if [ -n "$dts_path" ]; then
      cp "$dts_path" "$stage/maelys_playground.d.ts"
    fi

    local name="maelys-datalog-${version}-wasm-${suffix}.tar.gz"
    tar -czf "$dist/${name}" -C "$stage" .
    ( cd "$dist" && sha256 "${name}" > "${name}.sha256" )
    rm -rf "$stage"

    echo "packaged ${name}"
    artifacts+=("$name")
  }

  build_wasm_profile small build/wasm
  stage_wasm small build/wasm

  build_wasm_profile large build/wasm-large
  stage_wasm large build/wasm-large

  emsdk_recorded="$EMSDK_VERSION"
fi

if [ "${#artifacts[@]}" -eq 0 ]; then
  echo "error: nothing was packaged (native skipped via --wasm-only, WASM skipped/unavailable)" >&2
  exit 1
fi

# ---------------------------------------------------------------------------
# Release receipt (D3).
# ---------------------------------------------------------------------------
if ! command -v jq >/dev/null 2>&1; then
  echo "error: jq is required to write the release receipt" >&2
  exit 1
fi

commit="$(git rev-parse HEAD)"
date_utc="$(date -u +%Y-%m-%d)"
npm_channel="@maelys/datalog-wasm@${version}"

artifacts_json="[]"
for f in "${artifacts[@]}"; do
  hash="$(sha256 "$dist/$f" | awk '{print $1}')"
  artifacts_json="$(jq -c --arg file "$f" --arg sha "$hash" '. + [{"file":$file,"sha256":$sha}]' <<<"$artifacts_json")"
done

if [ "$emsdk_recorded" = "null" ]; then
  emsdk_json="null"
else
  emsdk_json="$(jq -n --arg v "$emsdk_recorded" '$v')"
fi

jq -n \
  --arg name "maelys-datalog" \
  --arg version "$version" \
  --arg tag "v${version}" \
  --arg commit "$commit" \
  --arg date "$date_utc" \
  --argjson emsdk "$emsdk_json" \
  --argjson artifacts "$artifacts_json" \
  --arg npm "$npm_channel" \
  '{name:$name, version:$version, tag:$tag, commit:$commit, date:$date, emsdk_version:$emsdk, artifacts:$artifacts, channels:{npm:$npm}}' \
  > "$dist/release-receipt.json"

echo "wrote $dist/release-receipt.json"
echo "==> artifacts in dist/:"
ls -1 "$dist"
