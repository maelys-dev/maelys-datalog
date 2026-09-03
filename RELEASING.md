# Releasing

Releases are produced by `.github/workflows/release.yml`, triggered only by an
annotated `v*` tag. Pushing the tag is the authorization ceremony; nothing
publishes without it.

## Cutting a release

From a clean, up-to-date `main`, write the `## [X.Y.Z] - <date>` entry in
`CHANGELOG.md`, then run one command:

```sh
scripts/cut-release.sh X.Y.Z[-alpha.N]
```

`VERSION` is the single source of truth for the version; it writes that file and
regenerates `include/maelys_datalog_version.h` from it
(`scripts/generate-version-header.sh`) — nothing hand-edits both files. It then
runs `make check` **locally** (including a containerized GCC/Linux gate) to
reject a broken bump before it ever reaches CI, opens a release PR, waits for
the required checks, merges it, and pushes the annotated `vX.Y.Z[-alpha.N]` tag.
Then approve the `release` environment when the `publish` job requests it.

`include/maelys_datalog_version.h` stays a normal committed header — so
`#include`-ing it works from a plain checkout with no build step — but `make
check-version-header` verifies it was produced by the generator from `VERSION`;
any manual edit that lets the two drift fails the build immediately, not after a
tag is already public.

### Manual equivalent

1. Bump `VERSION`, run `scripts/generate-version-header.sh` to regenerate
   `include/maelys_datalog_version.h`, plus `CHANGELOG.md`, on a PR merged to
   `main` after `CI` is green.
2. `git tag -a vX.Y.Z[-alpha.N] -m "maelys-datalog X.Y.Z[-alpha.N]" <merge-commit> && git push origin vX.Y.Z[-alpha.N]`
3. Approve the `release` environment.

The tag triggers a matrix build (Linux x86_64/arm64, macOS arm64) plus a
separate WASM build, then a separate `publish` job attaches the artifacts to
the GitHub Release. The build jobs have no write access and no secrets; the
publish job compiles nothing and only verifies checksums before uploading.

Finally, the npm package is published to the npm registry (see [npm package](#npm-package) below) —
this step is fully automated in the `publish` job after the human gate.

## Building the artifacts locally

The CI workflow and a local build share one script — a single source of truth:

```sh
scripts/package-release.sh            # target auto-detected from uname
scripts/package-release.sh linux-arm64  # or force a label
```

It produces the native archive (+ `.sha256`) for the current platform in `dist/`,
along with an optional WASM build when the pinned emsdk is available.

To reproduce a **Linux** build from macOS (or to check a clean environment), run
it in the pinned Ubuntu image — the container works on a copy, never the host
tree:

```sh
docker run --rm --platform linux/arm64 -v "$PWD:/source:ro" ubuntu:24.04 bash -c '
  apt-get update && apt-get install -y --no-install-recommends \
    build-essential cmake pkg-config curl ca-certificates
  cp -a /source /work && cd /work && rm -rf build dist .deps-static srcdeps stage
  scripts/package-release.sh'
```

To include WASM artifacts, pass `--wasm-only` to build both small and large
profiles. The pinned emsdk (`3.1.61`, defined in `scripts/package-release.sh`)
must be installed and activated:

```sh
git clone https://github.com/emscripten-core/emsdk.git
cd emsdk
./emsdk install 3.1.61
./emsdk activate 3.1.61
source ./emsdk_env.sh
# Then:
scripts/package-release.sh --wasm-only
```

The script verifies `emcc --version` matches the pinned version before building
and **fails otherwise** — no silent fallback to whatever is on PATH.

## Artifacts

Each platform ships **one or more tarballs**, all carrying a `.sha256` and a build
provenance attestation:

- `maelys-datalog-X.Y.Z-<target>.tar.gz` — native library and headers for the
  given target (linux-x86_64, linux-arm64, macos-arm64). Ships `lib/libmaelys_datalog.a`,
  `include/maelys_datalog.h`, `include/maelys_datalog_version.h`, `LICENSE`,
  and `CHANGELOG.md`.
- `maelys-datalog-X.Y.Z-wasm-small.tar.gz` — WebAssembly module and wrapper for
  the default profile (EDB bound 1024). Ships `maelys_datalog_dynamic.js`,
  `maelys_datalog_dynamic.wasm`, `maelys_playground.js`, and type definitions if available.
- `maelys-datalog-X.Y.Z-wasm-large.tar.gz` — WebAssembly module and wrapper for
  the extended profile (`-DMAELYS_DATALOG_PROFILE_LARGE`). Same layout as small.

**Single native variant (no dynamic/static split):** the engine has zero
third-party runtime dependencies (yyjson is vendored). One archive per platform
suffices.

Windows is not supported (untested toolchain, no CI runner budget). macOS Intel
(x86_64) is intentionally not shipped (same policy as mcp-runtime).

## Release receipt

`release-receipt.json`, produced by `scripts/package-release.sh` and uploaded
with the artifacts, proves what was built, when, and with which toolchain:

```json
{
  "name": "maelys-datalog",
  "version": "0.2.0",
  "tag": "v0.2.0",
  "commit": "<40-hex>",
  "date": "2026-08-20",
  "emsdk_version": "3.1.61",
  "artifacts": [
    { "file": "maelys-datalog-0.2.0-macos-arm64.tar.gz", "sha256": "…" }
  ],
  "channels": { "npm": "@maelys/datalog-wasm@0.2.0" }
}
```

The receipt is the junction with downstream release engineering: the receipt
serves as the input document for governance cycles and site deployments.

## npm package

The WASM artifacts (both small and large profiles) are bundled into the npm
package `@maelys/datalog-wasm` and published to the npm registry after the human
gate in the `release` environment.

```sh
# Locally (for testing):
scripts/build-npm-package.sh dist/

# In CI (via the publish job):
NPM_DIST_TAG=next scripts/build-npm-package.sh dist/ --publish
```

The package is published with:
- `npm publish --provenance --access public` for OIDC trusted publishing.
- dist-tag `next` for pre-release versions (e.g. `0.1.0-alpha.1`), `latest` for
  stable releases.
- No manual steps — fully automated in the `publish` job.

The package exports both profiles:

```javascript
// Default (small profile)
import Module from '@maelys/datalog-wasm';

// Large profile (if needed)
import Module from '@maelys/datalog-wasm/large';

// Interactive playground wrapper
import Playground from '@maelys/datalog-wasm/playground';
```

## Verifying provenance

Attestations link each tarball to the commit, workflow, and runner that built it:

```sh
gh attestation verify maelys-datalog-X.Y.Z-<target>.tar.gz \
  --repo maelys-dev/maelys-datalog
```

This proves the artifact was built by this repository's workflow from a specific
commit. It does not prove the artifact is free of vulnerabilities — provenance is
integrity of the process, not of the source.

## Before the first binary release (one-time setup)

- **Configure the `release` environment** (Settings → Environments) with a
  required reviewer — otherwise the approval gate is a no-op.
- **Enable branch protection on `main`** to require status checks and approvals
  before merge.
- **Restrict tag creation** to block accidental tag pushes outside the ceremony
  (protect `refs/tags/v*`).
- **Publish the repository** to GitHub so that build attestations and npm
  `--provenance` (OIDC trusted publishing) work; the chain does not function on
  private repos.
- **Configure npm `@maelys/datalog-wasm`**: register the package name (even an
  empty placeholder) on npm so that OIDC trusted publishing can authorize
  subsequent releases. This requires a manual `npm publish` of a placeholder
  version from a trusted npm account before the automated workflow can work.
- **Commit `include/maelys_datalog_version.h`** if not already committed; the
  file must exist in the repo for `make check-version-header` to pass.
- **Delete the stray tag** `update-2026-06-14_14-40-55-575` (predates this
  policy):
  ```sh
  git tag -d update-2026-06-14_14-40-55-575
  git push origin --delete update-2026-06-14_14-40-55-575
  ```

Workflow actions are already pinned to full SHA; update them via a dedicated PR
by re-resolving the desired version tag.

## Homebrew tap

Users install with `brew install maelys-dev/tap/maelys-datalog`. The formula
lives in [maelys-dev/homebrew-tap](https://github.com/maelys-dev/homebrew-tap)
(`Formula/maelys-datalog.rb`) and builds the static library from the release
source tarball, installing the full header tree (the public header includes
engine headers by repo-relative paths, so `src/core/`, `src/manifest/` and
`common/` headers ship under `include/`).

`cut-release.sh` bumps the formula automatically as its last step — it only
needs the pushed git tag, so it does not wait on the binary release workflow
or its approval gate. To bump it on its own:

```sh
scripts/update-tap-formula.sh          # defaults to the current VERSION's tag
scripts/update-tap-formula.sh v1.2.3   # or bump to any already-pushed tag
```
