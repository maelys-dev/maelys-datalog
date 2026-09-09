# Extension SDK validation — 2026-09-09

Scope: the uncommitted extension-envelope/context work on top of
`0.1.0-alpha.4`, not a published release or a remote CI verdict.
See the [architecture and migration guide](architecture/extension-contexts.md)
and [standalone examples](../sdk/examples/README.md).

## Observed results

| Check | Result |
| --- | --- |
| Full native Make inventory, SMALL and LARGE | PASS: 32 executables per profile; 672 existing checks plus the context suite |
| Full macOS ASan/UBSan inventory, SMALL and LARGE | PASS: all 32 executables per profile; macOS leak detection disabled |
| Linux ASan/UBSan/LSan, SMALL and LARGE | PASS: context, modules, compiler and pipeline suites in Ubuntu 24.04 arm64 with Clang 18 and leak detection enabled |
| CMake/CTest, SMALL and LARGE | PASS: 10 tests per profile, including static/shared contexts and the C Python shim |
| Installed SDK, SMALL and LARGE | PASS: five headers in C11/C++17; six opaque layouts rejected; external consumers and all four standalone projects in static/shared linkage |
| Python wrapper | PASS: 17 tests each in SMALL, LARGE and a repeated SMALL run |
| Shipped WASM wrapper | PASS in both profiles: 64 playground cases plus build-limits and manifest-stack checks |
| Statically linked WASM extensions | PASS: four conformance executables per profile, each containing the C host and provider in the same module |
| Legacy examples | PASS: `make -j2 examples` |
| Pipeline benchmark | PASS: 2,000 solves, 2,000 materializations, one preparation; 0.019 CPU seconds on this host |
| libFuzzer smoke | PASS: 10,000 runs |
| Guards | PASS: `actionlint`, SDK include boundaries, version-header consistency, shell syntax and `git diff --check` |

Native wrapper checks used macOS arm64, Python 3.14.6 and Node 26.3.0.
Local WASM builds used Emscripten 5.0.7-git. The workflow additionally specifies
Emscripten 3.1.61 and 4.0.14; those remote jobs were not run by this local check.
The CPU measurement is descriptive, not a throughput guarantee.

## Regression coverage added

- Invalid multi-kind registration rolls back without reserving its package name
  or publishing valid earlier components; oversized counts leave the catalogue intact.
- Descriptor and identity inputs can be stack-owned and modified after registration.
- Seal is one-way; missing named selections never silently fall back.
- Two concurrent contexts can use the same filter name with opposite semantics.
  Query results and Why-true/Why-false text resolve the correct catalogue.
- Context and policy handles can be released before their sessions finish.
- Cross-context policy selection and unsupported backend capabilities are rejected.
- Contextual loads neither populate nor seal the legacy registry; new contexts
  remain configurable after legacy sealing.
- An unselected registered planner does not activate; an out-of-range selected
  planner output fails atomically.
- Reversed filter registration order preserves policy, program and session
  fingerprints. Plain contextual and legacy policy identities agree.
- Existing pipeline golden transcripts remain unchanged; the benchmark still
  observes one materialization per solve.

## Issues found during validation

The new direct filter error fixture exposed missing null-argument guards in the
educational exact-match callback. Those guards were added and the installed
examples and WASM fixtures passed afterward; the host already guarded such
arguments on production dispatch.

Repeated Python profile switches triggered macOS `CODESIGNING / Invalid Page`
kills, although the libraries verified correctly on disk. The test helper now
copies native libraries to fresh files and renames them into place. The complete
SMALL → LARGE → SMALL sequence passed after this change. No Python API changed.

The optional local AFL++ attempt was stopped during compilation and is **not**
counted as validated. The successful libFuzzer smoke is not an AFL++ result or
an exhaustive fuzz campaign. Full Linux native/sanitizer inventories remain CI
work; the local Linux run above was intentionally limited to four SDK suites.

## Reproduction and delivery limits

Commands are in [the validation matrix](validation.md). New focused checks are
`tools/check_module_sdk.sh` and `tools/check_wasm_extensions.sh`.
Local transcripts were retained under `/private/tmp/maelys-context-*.log`;
these temporary files are not release artifacts.

The native packaging script now includes the extension header and conformance
kit; its shell syntax and installed-SDK contents were checked, but no release
tarball was published. No commit, push or deployment was performed.

Domains remain process-global. Contextual manifest loading and Python/JS
catalogue selectors are not implemented. There is no dynamic loader, new
Datalog grammar, proprietary algorithm, Gitolite frontend or regex provider.
The open reference solver and existing typed extension ABIs are preserved.

## Addendum: composite example

The fifth standalone project, `sdk/examples/bundle/`, combines a `permit`
frontend and an `exact_match` filter in a single declaration. Only the example,
documentation and SDK test inventories changed for this addition; no engine or
binding API changed.

Verified locally in SMALL and LARGE:

- All five projects through the installed-SDK script, in static/shared linkage,
  including the bundle copied outside the source tree.
- All five projects statically linked into WASM and run under Node.
- Bundle and full C host compiled together with Linux Clang 18 ASan/UBSan/LSan,
  with leak detection enabled; both profiles passed.

The bundle checks allowed/denied candidates, retained proofs, source positions,
source ownership, explicit selection, missing/incompatible filter rejection,
registration rollback, syntax failures and mandatory IR validation. Native,
WASM and sanitizer logs are respectively `/private/tmp/maelys-bundle-installed.log`,
`/private/tmp/maelys-bundle-wasm.log` and `/private/tmp/maelys-bundle-sanitizers.log`.
These are local results, not a claim that remote CI has run.

## Addendum: WASM binding layout

The C boundary/header and JavaScript wrapper were moved together into
`bindings/wasm/`, with builds, source imports and packaging paths updated.
Checksums confirmed an unchanged header, unchanged C implementation after its
include line, and unchanged JavaScript runtime after its documentation header.

After relocation, the full native Make inventory passed in SMALL and LARGE,
as did all three Node/WASM suites (64 playground cases plus limits and stack
checks per profile). The native binding suite passed its 39 cases under
macOS ASan/UBSan in both profiles.

The actual `stage_wasm` packaging function was also exercised independently of
the release build: both test archives kept the three expected root-level
basenames, matched the source/build files byte-for-byte, and loaded the wrapper
and WASM together under Node. Test archives are under
`/private/tmp/maelys-wasm-package-layout.IJmFBT`; no release receipt, publication
or claim of a pinned-toolchain release build was made.
