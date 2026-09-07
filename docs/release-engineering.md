# Release engineering — decisions (R0)

Status: **adopted** (2026-08-20). This page is the specification the release
tooling implements. The technique is ported from `mcp-runtime` (proven over 18
tags, up to v0.15.0); every deviation from that model is recorded here with its
reason. This repository does not use the shared `maelys-release` socle; D7
records why and what adopting it would cost. Nothing in this page changes the
language or the engine.

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

Because of that reach, `channels` records what **published**, never what was
planned. The build jobs write `channels: {}`; the `publish` job adds an entry
with `--record-channel` only after that channel's publication returned
success, and re-uploads the receipt to the Release. An empty `channels` is a
truthful statement that nothing shipped beyond the Release itself. This is a
correction: `v0.1.0-alpha.4` shipped a receipt asserting an npm package whose
publication had in fact failed with a 404, and that assertion would have been
carried to the public site.

## D4 — Version and tag policy

- Format: SemVer with optional pre-release, `X.Y.Z` or `X.Y.Z-alpha.N`
  (current: `0.1.0-alpha.4`). This is what keeps `maelys-release` out of
  reach; see D7. `cut-release.sh` accepts
  `^[0-9]+\.[0-9]+\.[0-9]+(-alpha\.[0-9]+)?$` — wider than mcp-runtime's
  stable-only regex, because this project releases alphas.
- Tag: annotated and signed `vX.Y.Z[-alpha.N]`, must equal `v$(cat VERSION)`.
  Signature is not checked by this repository's own `release.yml`, but the
  tags carry one and the socle of D7 requires it.
- The stray tag `update-2026-06-14_14-40-55-575` this section used to schedule
  for deletion no longer exists, locally or on the remote: only the
  `v0.1.0-alpha.*` tags remain.
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
every channel that publishes appears in the receipt (D3) — recorded after the
fact, so a failed channel leaves no trace claiming otherwise. A channel
requiring a manual step per release is a channel that will drift.

## D6 — Deviations from the mcp-runtime model

| Deviation | Reason |
|---|---|
| Single native variant (no dynamic/static split) | zero third-party runtime deps |
| WASM build matrix entry | product requirement; emsdk pinned per D2 |
| Pre-release-capable version regex | project ships alphas |
| npm publish step in `publish` job | D5; runs after the same human gate |
| `make check` may need creating | upstream Makefile has `test` but no `check`/`install`; R1/R2 add the missing targets rather than renaming existing ones |

## D7 — Relationship to `maelys-release`

`maelys-dev/maelys-release` is the shared release socle of the Maelys
repositories: a reusable `release.yml` called with `workflow_call`, a reusable
Homebrew tap workflow, a reusable product CI workflow, and the
`maelys-release` command that adopts them. **This repository has not adopted
it**, and the chain described above is its own, ported from `mcp-runtime`.
That is a decision, not an oversight, and it is recorded here so the gap is
visible rather than inferred from the absence of a `uses:` line.

Measured against socle **v0.15.3**, `maelys-release check` reports:

| Item | Verdict |
|---|---|
| `VERSION` | **violation** — the socle requires `X.Y.Z` and nothing else; this project ships `0.1.0-alpha.N` (D4) |
| `.github/workflows/ci.yml` | **warning** — does not call the socle's `check-product.yml` |
| `scripts/package-release.sh TARGET` writing `dist/` | conforms |
| `CHANGELOG.md` dated entry | conforms |
| `packaging/homebrew/*.rb.in` | absent, so no tap job — consistent with D5 |

`maelys-release adopt` therefore refuses this repository outright
(`PRECONDITION_FAILED`) as long as it releases pre-releases. The signed
annotated tag the socle demands is already produced: `v0.1.0-alpha.4` is
verified by GitHub.

Two product requirements also fall outside the socle's contract:

- **No npm channel.** The socle's inputs are `product`, `tag`,
  `dependency_checkout`, `linux_packages`, `macos_packages`,
  `package_command`, the three runners, `release_environment` and
  `attestation`. D5's npm channel — assembling the WASM profiles into
  `@maelys-dev/datalog-wasm` and publishing it — has no place to live there.
- **No WASM target.** The socle's matrix is linux-x86_64, linux-arm64 and
  macos-arm64. The two WASM profiles are built by a separate job here (D1),
  and the reproducibility pin of D2 is enforced by `package-release.sh`.

The consequence is accepted: this repository maintains its own
`release.yml` and `cut-release.sh`, and does not benefit from socle fixes.
The exit is equally explicit. Adopting the socle requires, in this order:
leaving the `0.x-alpha` series for plain `X.Y.Z` versions (which retires the
D4 deviation), adding the `check-product.yml` job to `ci.yml`, and deciding
where the npm and WASM channels live — either as product jobs beside the
socle's reusable workflow, or dropped. Until then, a socle release that
changes the product contract does not affect this repository, and
`maelys-release check` is the way to measure the distance again.

## Prerequisite (outside this tooling)

The GitHub repository must be published for any of `release.yml`, attestation,
`gh attestation verify`, or npm provenance to function. Until then the chain is
authorable and locally testable (`package-release.sh`, `cut-release.sh`
preconditions), but the ceremony cannot complete.
