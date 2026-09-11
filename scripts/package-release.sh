#!/usr/bin/env bash
#
# Build and package the release artifacts of maelys-datalog for ONE target:
#   - linux-x86_64, linux-arm64, macos-arm64 : lib/libmaelys_datalog.a + the
#     public headers, one tarball
#   - wasm32 : maelys_datalog_dynamic.{js,wasm} + the JS wrapper/types, one
#     tarball per memory profile (small, large)
#
# One command, used both locally and by the build job of maelys-release,
# which runs `scripts/package-release.sh TARGET` on one runner per target
# declared in maelys-release.conf and attests dist/* (docs/release-engineering.md
# D1, D7). Outputs to dist/; everything else happens in disposable staging
# directories that are removed before the script exits.
#
# Usage:
#   scripts/package-release.sh [TARGET]
#
#   TARGET   one of linux-x86_64, linux-arm64, macos-arm64, wasm32. Defaults
#            to <os>-<arch> detected from uname; any other OS/arch, detected
#            or explicit, is refused (D1).
#
# wasm32 needs emcc pinned to exactly $EMSDK_VERSION (D2). An emcc of that
# version on PATH is used as is; otherwise the script installs the pinned
# emsdk itself under $EMSDK_DIR (default: $RUNNER_TEMP/emsdk on a runner,
# build/emsdk locally) and activates it for this process only. A mismatched
# emcc on PATH is never used: no silent fallback to whatever is installed.
#
# Every run writes dist/release-receipt-<TARGET>.json, the immutable record
# of what this target built (D3). The receipts are per target and never
# merged: the socle's SHA256SUMS lists them beside the archives, and what
# happened after the build — a channel publication — is recorded by the
# socle in channel-<name>.json, never here.
set -euo pipefail

# Pinned emsdk version (D2). Update deliberately, in a dedicated PR, the same
# way action SHAs are bumped — never silently.
EMSDK_VERSION="3.1.61"

NATIVE_TARGETS="linux-x86_64 linux-arm64 macos-arm64"
KNOWN_TARGETS="$NATIVE_TARGETS wasm32"

root="$(cd "$(dirname "$0")/.." && pwd)"
cd "$root"

# Portable SHA-256 (Linux: sha256sum, macOS: shasum -a 256).
sha256() {
  if command -v sha256sum >/dev/null 2>&1; then sha256sum "$@"; else shasum -a 256 "$@"; fi
}

usage() {
  echo "Usage: scripts/package-release.sh [TARGET]   (TARGET: $KNOWN_TARGETS)" >&2
}

target=""
while [ $# -gt 0 ]; do
  case "$1" in
    -h|--help) usage; exit 0 ;;
    -*) echo "error: unknown flag: $1" >&2; usage; exit 1 ;;
    *)
      [ -z "$target" ] || { echo "error: unexpected argument: $1" >&2; usage; exit 1; }
      target="$1"; shift ;;
  esac
done

version="$(cat VERSION)"
dist="$root/dist"
mkdir -p "$dist"

# ---------------------------------------------------------------------------
# Target detection / validation (D1: only these four are shipped).
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
if [ "$target" != wasm32 ]; then
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
  cp include/maelys/datalog.h include/maelys/datalog_module.h \
     include/maelys/datalog_program.h include/maelys/datalog_backend.h \
     include/maelys/datalog_extension.h "$stage/include/maelys/"
  mkdir -p "$stage/share/maelys-datalog/conformance"
  cp sdk/conformance/maelys_conformance.h sdk/conformance/README.md \
     "$stage/share/maelys-datalog/conformance/"
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
# ---------------------------------------------------------------------------
# WASM artifacts (D1 wasm32 row: small and large profiles, D2 pinned emsdk).
# ---------------------------------------------------------------------------
# The first run of a freshly activated emcc prints its cache notices before
# the version line, so the version is looked for in the whole output.
emcc_is_pinned() {
  command -v emcc >/dev/null 2>&1 || return 1
  emcc --version 2>&1 | grep -q "^emcc .* ${EMSDK_VERSION} "
}

# Install the pinned emsdk when no emcc of that version is on PATH. The
# checkout is reused across runs (a local build/emsdk, or the runner's temp
# directory); the activation touches only this process's environment.
ensure_pinned_emsdk() {
  if emcc_is_pinned; then
    echo "==> emcc ${EMSDK_VERSION} found on PATH"
    return 0
  fi
  local dir="${EMSDK_DIR:-${RUNNER_TEMP:-$root/build}/emsdk}"
  if command -v emcc >/dev/null 2>&1; then
    echo "note: emcc on PATH ($(emcc --version 2>&1 | head -n1)) is not emsdk ${EMSDK_VERSION}; installing the pinned one under ${dir}" >&2
  else
    echo "==> emcc not on PATH; installing emsdk ${EMSDK_VERSION} under ${dir}"
  fi
  if [ ! -x "$dir/emsdk" ]; then
    git clone -q --depth 1 https://github.com/emscripten-core/emsdk.git "$dir"
  fi
  ( cd "$dir" && ./emsdk install "$EMSDK_VERSION" && ./emsdk activate "$EMSDK_VERSION" ) >/dev/null
  # emsdk_env.sh reads variables it does not set; it is not written for set -u.
  set +u
  # shellcheck disable=SC1091
  source "$dir/emsdk_env.sh" >/dev/null 2>&1
  set -u
  emcc_is_pinned || {
    echo "error: emsdk ${EMSDK_VERSION} activated under ${dir} but emcc does not report it: $(emcc --version 2>&1 | head -n1)" >&2
    exit 1
  }
}

if [ "$target" = wasm32 ]; then
  ensure_pinned_emsdk

  dts_path=""
  if [ -f "bindings/wasm/maelys_playground.d.ts" ]; then
    dts_path="bindings/wasm/maelys_playground.d.ts"
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
    cp bindings/wasm/maelys_playground.js "$stage/"
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

# ---------------------------------------------------------------------------
# Release receipt (D3): one per target, immutable, listed by SHA256SUMS.
# ---------------------------------------------------------------------------
if ! command -v jq >/dev/null 2>&1; then
  echo "error: jq is required to write the release receipt" >&2
  exit 1
fi

commit="$(git rev-parse HEAD)"
date_utc="$(date -u +%Y-%m-%d)"
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

receipt="$dist/release-receipt-${target}.json"
jq -n \
  --arg name "maelys-datalog" \
  --arg version "$version" \
  --arg tag "v${version}" \
  --arg commit "$commit" \
  --arg date "$date_utc" \
  --arg target "$target" \
  --argjson emsdk "$emsdk_json" \
  --argjson artifacts "$artifacts_json" \
  '{name:$name, version:$version, tag:$tag, commit:$commit, date:$date, target:$target, emsdk_version:$emsdk, artifacts:$artifacts}' \
  > "$receipt"

echo "wrote $receipt"
echo "==> artifacts in dist/:"
ls -1 "$dist"
