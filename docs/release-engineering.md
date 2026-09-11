# Release engineering — decisions (R0)

Status: **adopted** (2026-08-20). This page is the specification the release
tooling implements. The technique is ported from `mcp-runtime` (proven over 18
tags, up to v0.15.0); every deviation from that model is recorded here with its
reason. Since the migration recorded in D7, the mechanism itself is the
`maelys-release` socle's: this page keeps the invariants and the decisions
the socle does not make for this product. Nothing in this page changes the
language or the engine.

## Invariants (ported unchanged from mcp-runtime)

1. **`VERSION` is the single source of truth.** The public version header is
   *generated* from it; a build-time guard fails if the two drift.
2. **One packaging script** (`scripts/package-release.sh TARGET`),
   byte-identical in local use and CI: the socle's build job runs it as is,
   one runner per target. Anything the script downloads is pinned by exact
   version.
3. **One cutting command** with hard preconditions: clean tree, `HEAD ==
   origin/main`, tag free, CHANGELOG entry written. The command is the
   socle's `maelys-release cut`; the local check plus the containerised
   second-compiler check *before* any tag exists are this product's
   `scripts/release-gates.sh`, run first (the socle cannot know them).
4. **The annotated tag is the authorization ceremony.** The socle's
   `release.yml` triggers only on `v*`, verifies `tag == v$(cat VERSION)` and
   the tag's signature through the GitHub API.
5. **Privilege separation in CI.** The build jobs execute candidate code and
   hold **no** write permission and no secrets (`contents: read`, OIDC +
   attestations only). The publish job holds `contents: write` and
   **compiles nothing** — it verifies checksums and attaches artifacts.
   Between the two: the `release` GitHub environment with a required
   reviewer — the material human gate, declared `[gate] reviewer` in
   `maelys-release.conf` so that `preflight` refuses to cut when it is
   unarmed.
6. **Provenance attestation** for every artifact; workflow actions pinned to
   full 40-hex SHAs — in the socle, whose workflows this repository pins by
   SHA in turn.
7. **`RELEASING.md`** documents the whole ceremony, including one-time setup.

## D1 — Artifact matrix

Unlike mcp-runtime (which needs dynamic/static variants for jansson/uriparser),
the engine has **zero third-party runtime dependencies** (yyjson is vendored).
One native variant suffices. The targets are declared in `maelys-release.conf`
and rendered by the socle into the build matrix; `wasm32` is a fourth
target on an Ubuntu runner, not a separate job.

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
- The script uses an `emcc` of that exact version when one is on PATH, and
  otherwise installs the pinned emsdk itself (under `$EMSDK_DIR`,
  `$RUNNER_TEMP/emsdk` on a runner) and activates it for its own process —
  the socle's runner has no Emscripten and the script owns the pin. A
  mismatched `emcc` on PATH is never used: no silent fallback to whatever is
  installed.
- The receipt (D3) records `emsdk_version` and the sha256 of every WASM
  artifact. The site's `sync-wasm.mjs` already refuses artifacts that do not
  match their receipt byte-for-byte; that contract is unchanged, the receipt
  simply gains release-level fields.
- Full bit-reproducibility across *machines* is a goal, not a gate, for the
  first tooled release: the gate is "CI artifacts match their own receipt and
  the site consumes only receipt-bound bytes". A `REPRODUCIBILITY.md` note may
  tighten this later.

## D3 — Release receipt

One receipt **per target**, `release-receipt-<target>.json`, written by
`package-release.sh` beside the artifacts, listed in `SHA256SUMS` (declared
under `[manifest]` in `maelys-release.conf`) and attested like any artifact:

```json
{
  "name": "maelys-datalog",
  "version": "0.3.0",
  "tag": "v0.3.0",
  "commit": "<40-hex>",
  "date": "2026-09-20",
  "target": "wasm32",
  "emsdk_version": "3.1.61",
  "artifacts": [
    { "file": "maelys-datalog-0.3.0-wasm-small.tar.gz", "sha256": "…" },
    { "file": "maelys-datalog-0.3.0-wasm-large.tar.gz", "sha256": "…" }
  ]
}
```

`emsdk_version` is `null` on native targets. A receipt is **immutable**: it
states what one build produced and is never rewritten afterwards.

The receipt is the junction with the governance cycles: the downstream site
cycle in `maelys-dl-site-engineering` takes the receipts as its input
documents, and the site's public version line (home page) is generated from
them — the internal proof apparatus and the visible one share a single source.

Because of that reach, a receipt says nothing about channels. What published
**after** the build is recorded by the socle, per channel, in a separate
asset `channel-<name>.json` attached to the Release only once that channel's
publication returned success (`{product, tag, channel, published, run}` plus
what `scripts/publish-channel.sh` recorded: registry, package, version,
dist-tag, `already_published`). A Release without `channel-npm.json` is a
truthful statement that nothing shipped beyond the Release itself. This
carries over the correction made after `v0.1.0-alpha.4`, whose receipt
asserted an npm package whose publication had in fact failed with a 404:
the observation of a publication lives in a file the build could not have
written. The site consumes the receipts for bytes and the markers for
channels.

## D4 — Version and tag policy

- Format: `X.Y.Z`, no pre-release suffix — the shape `maelys-release cut`
  accepts, the same as mcp-runtime's and the socle conventions' (D7). The `0.` already states that the API
  may break between minor versions, so `-alpha.N` restated it for nothing
  while making this repository unadoptable. The alpha series ended at
  `0.1.0-alpha.4`, which stays published and tagged as it is; the next
  release is `0.2.0`. Published alpha tags are never rewritten.
- Tag: annotated and signed `vX.Y.Z` on the merge commit of the release
  pull request, must equal `v$(cat VERSION)`; the socle's `release.yml`
  verifies the signature through the GitHub API before building.
- The stray tag `update-2026-06-14_14-40-55-575` this section used to schedule
  for deletion no longer exists, locally or on the remote: only the
  `v0.1.0-alpha.*` tags remain.
- CHANGELOG: one dated `## X.Y.Z — YYYY-MM-DD` entry per release, the
  conventions' format (no brackets, em dash). The cut refuses to run without
  the entry for the version being cut. Entries already published in the
  Keep-a-Changelog bracket form are left as they are: `maelys-release check`
  reads the entry for `VERSION` only, and rewriting history would serve
  nothing.

## D5 — Channels

| Channel | First tooled release | Rationale |
|---|---|---|
| GitHub Release tarballs + attestation | **yes** | the base layer |
| npm `@maelys-dev/datalog-wasm` on **GitHub Packages** | **yes**, dist-tag `next` while `0.x` | cheapest channel, platform-independent artifact, direct continuation of the playground; declared `[channels] npm github-packages` and published by the socle's channel job after the Release, through `scripts/publish-channel.sh` on the Release's own downloaded assets, authenticated by the run's `GITHUB_TOKEN` (`packages: write`). No long-lived registry secret and no trusted-publisher setup; in exchange the scope must be the repository owner's, consumers must authenticate even for a public package, and `npm publish --provenance` is unavailable — provenance stays on the tarball attestations |
| Homebrew tap (lib + header formula) | yes **iff** the port of `update-tap-formula.sh` stays under half a day; otherwise next pass | infrastructure and technique exist (`maelys-dev/homebrew-tap`); audience is narrow until a CLI exists |
| PyPI wheels | **no** | cibuildwheel matrix is a dedicated cycle; PyPI is irreversible and the cffi API is not frozen. Immediate actions only: reserve the name, add `pyproject.toml` for editable installs |

**Channel rule (binding):** a channel exists only if it hangs off the tag
ceremony and is fully automated inside the socle's `release.yml`, its
publication is idempotent on a replayed tag, and every channel that publishes
leaves its `channel-<name>.json` marker (D3) — recorded after the fact, so a
failed channel leaves no trace claiming otherwise. A channel requiring a
manual step per release is a channel that will drift.

## D6 — Deviations from the mcp-runtime model

| Deviation | Reason |
|---|---|
| Single native variant (no dynamic/static split) | zero third-party runtime deps |
| `wasm32` target in the matrix | product requirement; emsdk pinned and self-installed per D2 |
| npm channel after the Release | D5; runs after the same human gate, on the Release's own assets |
| Receipts per target, channels recorded apart | D3; a build writes only what it observed |
| The mechanism is the socle's, not a local port | D7 |

## D7 — Relationship to `maelys-release`

`maelys-dev/maelys-release` is the shared release socle of the Maelys
repositories: a reusable `release.yml` called with `workflow_call`, a reusable
channel workflow, a reusable Homebrew tap workflow, a reusable product CI
workflow, and the `maelys-release` command that adopts, checks, rehearses and
cuts. This repository ported its mechanism from `mcp-runtime` first, aligned
on the socle's conventions at `0.2.0` (its own `release.yml` still cutting),
and **now releases through the socle**: `.github/workflows/release.yml` is
rendered by `maelys-release adopt` from `maelys-release.conf` and is never
edited by hand.

What kept the mechanism local until socle v0.34 has been contributed
upstream from this repository's requirements and no longer exists:

| Requirement | Socle answer |
|---|---|
| A WASM target beside the three native ones (D1) | `[targets]` with a runner per row, v0.24 |
| Receipts and other files under the manifest (D3) | `[manifest]`, v0.24 |
| An npm channel after the Release (D5) | `[channels]` and `channel.yml`, v0.25; `channel-<name>.json` markers, v0.32 |
| The human gate refused when unarmed (invariant 5) | `[gate] reviewer` and `preflight`, v0.30 |
| Build jobs without write permission (invariant 5) | `contents: read`, v0.31 |
| A cut that waits for the checks to exist (invariant 3) | `cut`, v0.33 |
| A cut that runs the product's gate first and commits what the bump regenerates (invariants 1, 3) | `verify-release.sh` at the first stop and `[cut] after-version`, v0.34 |

What this product still owns, because the socle cannot know it:
`maelys-release.conf` and `dependencies/packages` (the declarations),
`scripts/package-release.sh` (D1, D2, D3), `scripts/verify-release.sh`
(what `make check` means here), `scripts/publish-channel.sh` (how npm is
published, idempotently), `scripts/generate-version-header.sh` (what a
version bump regenerates, named under `[cut]`), `scripts/release-gates.sh`
(the second-compiler gate of invariant 3, run before `cut`), and the pins
these scripts carry.

An upgrade of the socle is its own pull request: `maelys-release adopt`
regenerates `release.yml`, the managed blocks of `AGENTS.md` and `CLAUDE.md`
and the agent skill, and `ci.yml`'s `uses:` line; `check` reports the drift.

## Prerequisite (outside this tooling)

The GitHub repository must be published for any of `release.yml`, attestation
or `gh attestation verify` to function. The chain is locally testable without
a tag: `package-release.sh TARGET`, `release-gates.sh`, `maelys-release
rehearse . linux-arm64` (the socle's build job replayed in Docker) and the
plan-only stops of `maelys-release cut`.
