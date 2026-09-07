# Release engineering — decisions (R0)

Status: **adopted** (2026-08-20). This page is the specification the release
tooling implements. The technique is ported from `mcp-runtime` (proven over 18
tags, up to v0.15.0); every deviation from that model is recorded here with its
reason. Nothing in this page changes the language or the engine.

## Invariants (ported unchanged from mcp-runtime)

1. **`VERSION` is the single source of truth.** The public version header is
   *generated* from it; a build-time guard fails if the two drift.
2. **One packaging script** (`scripts/package-release.sh`), byte-identical in
   local use and CI. Anything the script downloads is pinned by exact version
   *and* checksum.
3. **One cutting command** (`scripts/cut-release.sh X.Y.Z[-alpha.N]`) with hard
   preconditions: clean tree, `HEAD == origin/main`, tag free, CHANGELOG entry
   written. Local check plus containerised second-compiler check *before* any
   tag exists.
4. **The annotated tag is the authorization ceremony.** `release.yml` triggers
   only on `v*` and verifies `tag == v$(cat VERSION)`.
5. **Privilege separation in CI.** The `build` jobs execute candidate code and
   hold **no** write permission and no secrets (OIDC + attestations only). The
   `publish` job holds `contents: write` and **compiles nothing** — it verifies
   checksums and attaches artifacts. Between the two: the `release` GitHub
   environment with a required reviewer — the material human gate.
6. **Provenance attestation** for every artifact; workflow actions pinned to
   full 40-hex SHAs.
7. **`RELEASING.md`** documents the whole ceremony, including one-time setup.

## D1 — Artifact matrix

Unlike mcp-runtime (which needs dynamic/static variants for jansson/uriparser),
the engine has **zero third-party runtime dependencies** (yyjson is vendored).
One native variant suffices.

| Artifact | Targets | Contents |
|---|---|---|
| `maelys-datalog-X.Y.Z-<target>.tar.gz` | linux-x86_64, linux-arm64, macos-arm64 | `lib/libmaelys_datalog.a`, `include/maelys_datalog.h`, `include/maelys_datalog_version.h`, `LICENSE`, `CHANGELOG.md` |
| `maelys-datalog-X.Y.Z-wasm-small.tar.gz` | wasm32 (profile `small`) | `maelys_datalog_dynamic.js`, `maelys_datalog_dynamic.wasm`, `maelys_playground.js`, `maelys_playground.d.ts` |
| `maelys-datalog-X.Y.Z-wasm-large.tar.gz` | wasm32 (profile `large`, `-DMAELYS_DATALOG_PROFILE_LARGE`) | same layout |

Every tarball ships with a `.sha256` sibling and a provenance attestation.
macOS Intel is intentionally not shipped (same policy as mcp-runtime).
Windows is out of scope (untested toolchain, no CI runner budget for it).

## D2 — WASM reproducibility

The equivalent of mcp-runtime's SHA-pinned jansson is the **pinned emsdk**:

- `EMSDK_VERSION` is fixed in `package-release.sh` (initially `3.1.61`; update
  deliberately, in a dedicated PR, like an action SHA bump).
- The script verifies `emcc --version` matches before building and **fails
  otherwise** — no silent fallback to whatever is on PATH.
- The receipt (D3) records `emsdk_version` and the sha256 of every WASM
  artifact. The site's `sync-wasm.mjs` already refuses artifacts that do not
  match their receipt byte-for-byte; that contract is unchanged, the receipt
  simply gains release-level fields.
- Full bit-reproducibility across *machines* is a goal, not a gate, for the
  first tooled release: the gate is "CI artifacts match their own receipt and
  the site consumes only receipt-bound bytes". A `REPRODUCIBILITY.md` note may
  tighten this later.

## D3 — Release receipt

`release-receipt.json`, produced by `package-release.sh`, uploaded with the
artifacts and attached to the GitHub Release:

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
  "channels": { "npm": "@maelys-dev/datalog-wasm@0.2.0" }
}
```

The receipt is the junction with the governance cycles: the downstream site
cycle in `maelys-dl-site-engineering` takes the receipt as its input document,
and the site's public version line (home page) is generated from it — the
internal proof apparatus and the visible one share a single source.

## D4 — Version and tag policy

- Format: SemVer with optional pre-release, `X.Y.Z` or `X.Y.Z-alpha.N`
  (current: `0.1.0-alpha.1`). `cut-release.sh` accepts
  `^[0-9]+\.[0-9]+\.[0-9]+(-alpha\.[0-9]+)?$` — wider than mcp-runtime's
  stable-only regex, because this project releases alphas.
- Tag: annotated `vX.Y.Z[-alpha.N]`, must equal `v$(cat VERSION)`.
- The stray tag `update-2026-06-14_14-40-55-575` predates this policy; delete
  it locally and on the remote when the repo is published (recorded here, not
  automated).
- CHANGELOG: Keep-a-Changelog format (already in place). The cut refuses to
  run without a `## [X.Y.Z] - <date>` entry.

## D5 — Channels

| Channel | First tooled release | Rationale |
|---|---|---|
| GitHub Release tarballs + attestation | **yes** | the base layer |
| npm `@maelys-dev/datalog-wasm` on **GitHub Packages** | **yes**, dist-tag `next` while alpha | cheapest channel, platform-independent artifact, direct continuation of the playground; published from the `publish` job (after the human gate) against `npm.pkg.github.com`, authenticated by the run's `GITHUB_TOKEN` (`packages: write`). No long-lived registry secret and no trusted-publisher setup; in exchange the scope must be the repository owner's, consumers must authenticate even for a public package, and `npm publish --provenance` is unavailable — provenance stays on the tarball attestations |
| Homebrew tap (lib + header formula) | yes **iff** the port of `update-tap-formula.sh` stays under half a day; otherwise next pass | infrastructure and technique exist (`maelys-dev/homebrew-tap`); audience is narrow until a CLI exists |
| PyPI wheels | **no** | cibuildwheel matrix is a dedicated cycle; PyPI is irreversible and the cffi API is not frozen. Immediate actions only: reserve the name, add `pyproject.toml` for editable installs |

**Channel rule (binding):** a channel exists only if it hangs off the tag
ceremony and is fully automated inside `cut-release.sh` → `release.yml`, and
every channel appears in the receipt. A channel requiring a manual step per
release is a channel that will drift.

## D6 — Deviations from the mcp-runtime model

| Deviation | Reason |
|---|---|
| Single native variant (no dynamic/static split) | zero third-party runtime deps |
| WASM build matrix entry | product requirement; emsdk pinned per D2 |
| Pre-release-capable version regex | project ships alphas |
| npm publish step in `publish` job | D5; runs after the same human gate |
| `make check` may need creating | upstream Makefile has `test` but no `check`/`install`; R1/R2 add the missing targets rather than renaming existing ones |

## Prerequisite (outside this tooling)

The GitHub repository must be published for any of `release.yml`, attestation,
`gh attestation verify`, or npm provenance to function. Until then the chain is
authorable and locally testable (`package-release.sh`, `cut-release.sh`
preconditions), but the ceremony cannot complete.
