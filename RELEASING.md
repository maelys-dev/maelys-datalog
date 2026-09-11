# Releasing

Releases are produced by the `maelys-release` socle: `.github/workflows/release.yml`
is rendered by `maelys-release adopt` from [`packaging/release`](packaging/release)
and triggers only on a signed, annotated tag `vX.Y.Z` pushed to `main`. The
decisions behind the ceremony are in
[`docs/release-engineering.md`](docs/release-engineering.md); this page is the
operator's sequence. Nothing below runs from a branch, and a published tag is
never moved, recreated or force-pushed: a failed publication is replayed on
the existing tag.

`maelys-release` is the command of a checkout of `maelys-dev/maelys-release`
at the tag `release.yml` pins (`bin/maelys-release`); every command plans
without `--apply` and writes with it.

## 1. Before anything is written

Write the changelog entry `## X.Y.Z — YYYY-MM-DD` under `[Unreleased]`, on
`main`, through an ordinary pull request. The cut refuses to run without it.

Then run the two local gates on the tree that will be released:

```bash
scripts/release-gates.sh
```

Gate 1 is `scripts/verify-release.sh` on this machine (clang); `cut` runs it
again itself, so gate 1 is the fast answer before gate 2. Gate 2 is `make
check CC=gcc` in a pinned `ubuntu:24.04` container on a disposable copy of
the tree — the second compiler, which no runner of the release matrix uses,
before any tag exists. `--skip-container` exists and says loudly that it
skipped.

## 2. First stop: the release pull request

```bash
maelys-release cut . X.Y.Z --apply
```

`cut` refuses a version that does not follow the current one, a worktree
carrying anything but `VERSION` and `CHANGELOG.md`, a `HEAD` that is not
`main` up to date with `origin`, and anything `preflight` holds: the signing
configuration, the previous tag, a free `vX.Y.Z`, and the `release`
environment armed with a reviewer (`[gate] reviewer`). It then writes
`VERSION`, regenerates the version header, commits both signed on
`release/vX.Y.Z`, opens the pull request and waits for the checks of that
commit to exist and to finish.

Before writing anything, `cut` also runs `scripts/verify-release.sh` with
this machine's target — `make check`, so the first stop lasts as long as it
does — and, after writing `VERSION`, the `[cut] after-version` command of
`packaging/release`: `scripts/generate-version-header.sh`, whose regenerated
`include/maelys_datalog_version.h` joins the bump commit. A failure of either
restores `VERSION` and creates nothing.

## 3. Middle stop: GitHub

Merge the release pull request when its checks are green. `cut` never merges
its own pull request.

## 4. Second stop: the tag

```bash
maelys-release cut . X.Y.Z --tag --apply
```

`cut --tag` reads the merged pull request, takes its merge commit, verifies it
is on `main` with `VERSION` = `X.Y.Z` and the dated entry, waits for every
check of that commit to be green, signs the tag on it (annotation: the
changelog entry) and pushes. The push triggers `release.yml`.

## 5. What the workflow does

1. Verifies through the GitHub API that the tag is signed and names
   `VERSION`.
2. One build job per target of `packaging/release` — `linux-x86_64`,
   `linux-arm64`, `macos-arm64`, and `wasm32` on an Ubuntu runner — each
   with `contents: read` only: installs the packages of
   `dependencies/packages` (`clang`, `jq` on Linux), runs
   `scripts/verify-release.sh TARGET` (`make check`), then
   `scripts/package-release.sh TARGET` writing `dist/`, then attests the
   provenance of `dist/*`.
3. Stops at the `release` environment: a required reviewer approves in the
   GitHub UI. Nothing after this line runs candidate code.
4. The publish job (`contents: write`, compiles nothing) verifies every
   `.sha256`, writes `SHA256SUMS` over the archives and the receipts, and
   creates the GitHub Release with them.
5. The channel job publishes `@maelys-dev/datalog-wasm` to GitHub Packages
   through `scripts/publish-channel.sh vX.Y.Z npm`, from the Release's own
   downloaded assets, then attaches `channel-npm.json` to the Release — the
   observation that it published, never written by a build.

Replay a tag whose publication failed, after fixing the cause or adopting a
corrected socle:

```bash
gh workflow run release.yml -f tag=vX.Y.Z
```

`publish-channel.sh` exits 0 without republishing a version the registry
already holds, so a replay is safe.

## Artifacts

Per release, attached to the GitHub Release:

| File | Produced by |
|---|---|
| `maelys-datalog-X.Y.Z-<target>.tar.gz` + `.sha256` for the three native targets | `package-release.sh <target>` — `lib/libmaelys_datalog.a`, the public headers, `LICENSE`, `licenses/yyjson/LICENSE`, `CHANGELOG.md`, the conformance kit |
| `maelys-datalog-X.Y.Z-wasm-small.tar.gz`, `-wasm-large.tar.gz` + `.sha256` | `package-release.sh wasm32` — `maelys_datalog_dynamic.{js,wasm}`, `maelys_playground.js` |
| `release-receipt-<target>.json`, one per target | `package-release.sh`; immutable: name, version, tag, commit, date, target, `emsdk_version` (`null` on native targets), artifacts with their sha256 |
| `SHA256SUMS` | the socle's publish job, over every archive and receipt |
| `channel-npm.json` | the socle's channel job, after the npm publication succeeded |

Provenance: `gh attestation verify <file> --repo maelys-dev/maelys-datalog`.

## Building locally

```bash
scripts/package-release.sh              # native, target detected from uname
scripts/package-release.sh wasm32       # both WASM profiles
scripts/build-npm-package.sh dist/      # assemble the npm package (pack only)
```

`wasm32` needs emsdk `3.1.61` (pinned in `package-release.sh`): an `emcc` of
that version on PATH is used, otherwise the script installs it under
`build/emsdk` (`$EMSDK_DIR` to choose) and activates it for its own run. Any
other target than the four above is refused.

`maelys-release rehearse . linux-arm64` replays the socle's Linux build job in
Docker — the closest thing to the workflow without a tag.

## The npm package

`@maelys-dev/datalog-wasm` is published to **GitHub Packages**
(`npm.pkg.github.com`), not to npmjs.com, by the socle's channel job with the
run's `GITHUB_TOKEN`; the dist-tag is `next` while the version is `0.x`,
`latest` from `1.0.0`. Consumers point the scope at that registry and
authenticate with a token carrying `read:packages` — GitHub Packages requires
it even for a public package:

```
@maelys-dev:registry=https://npm.pkg.github.com
//npm.pkg.github.com/:_authToken=${GITHUB_TOKEN}
```

The package's visibility is a repository setting, not a publish flag: a first
publication is private until made public in the package's settings.

## Socle upgrades

An upgrade of `maelys-release` is its own pull request: with a checkout of the
new socle tag, `maelys-release adopt . --mechanism maelys-release --apply`
regenerates `release.yml`, the managed blocks of `AGENTS.md` and `CLAUDE.md`,
the agent skill and the `uses:` line of `ci.yml`; `maelys-release check .`
reports any drift and must end on two `ok` verdicts before merging.

## One-time setup (done)

- The `release` environment exists with `davidb987654` as required reviewer,
  admins cannot bypass, deployment restricted to tags `v*`.
- `main` is protected: the five CI checks required, linear history, no force
  push or deletion, conversation resolution required, administrators
  included.
- Tags are signed (`tag.gpgsign`, `commit.gpgsign`) with the SSH key
  registered on GitHub; `preflight` verifies the configuration before a cut.
- The repository is public: attestations and `gh attestation verify` need it.
