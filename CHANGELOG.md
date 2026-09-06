# Changelog

All notable changes to Maelys Datalog are documented in this file.

The project follows [Semantic Versioning](https://semver.org/) and uses the
format described by [Keep a Changelog](https://keepachangelog.com/).

## [Unreleased]

### Added

- Backend ABI v2: optional EXPLAIN_FALSE capability and read-only Why-false text
  API, backed by the existing bounded reference diagnostic extractor. The naive
  backend returns UNSUPPORTED. ABI v1 backend descriptors must be rebuilt.
- Dedicated public MALFORMED_PROGRAM diagnostic for structurally invalid IR.
- Public validated program IR, explicit per-load frontends, source locations and
  per-session solver backends with capability negotiation and atomic failure.
- Independent public-only arrow-language and naive positive-Datalog examples,
  including recursive differential tests and typed IR round-trip validation.
- Separate compiled-program and execution fingerprints covering domain/schema,
  query restrictions, frontend/backend semantic identities and execution options.
- Versioned public C SDK for separately compiled string-filter and join-planner
  modules, with bounded startup registration and immutable module identities.
- Installed-SDK consumer tests, static/shared integration tests, callback-error
  and budget checks, and cross-process semantic fingerprint tests.

### Changed

- Standard inline compilation now uses the generic frontend pipeline, with one
  common validation pass and a compiled-program fingerprint cached at finalization.
  The reference backend reuses the runtime's prepared session and materialized
  inputs. Legacy identity/proof transcripts have SMALL/LARGE regression goldens;
  grammar, solver algorithm and language bindings are unchanged.
- The opaque native C session API dispatches through the reference adapter by
  default. Existing source authority fingerprints, results and proof formatting
  are preserved; legacy/Python/WASM entrypoints remain on the reference engine.
- Binding safety, structural checks and stratification are shared by all
  frontends. No parser hooks or mutable grammar registry are introduced.
- Standard string filters now live in `modules/standard/` and use the same SDK
  as external modules; the default behavior and standard fingerprints remain.
- Extended policies bind filter/planner semantics into their executable identity
  after source integrity verification. Module failures remain fail-closed.
- All build variants use shared source manifests. Public CMake include paths no
  longer expose private engine headers; native packages include the module SDK.

- The `MAELYS-DATALOG-WHY-FALSE-v1` text is part of the public explain-false
  contract: limit hits are named (`none`, `candidate-rules`, `substitutions`,
  `depth`, `diagnostics`, `filter-cost`) instead of a raw bitmask, and `?N` /
  `binding=N` are documented as rule-local IR variable ids. The reference's
  bounds are fixed in backend ABI v2.

### Fixed

- Native and npm packages preserve the vendored yyjson license notice.

## [0.1.0-alpha.3] - 2026-09-03

### Fixed

- The WebAssembly build links `maelys_datalog_filter.c`, which the native
  builds already compiled; the release workflow of `v0.1.0-alpha.2` failed
  on its undefined filter symbols, so that tag has no release.

## [0.1.0-alpha.2] - 2026-09-03

### Changed

- Relicense from MIT to the Mozilla Public License 2.0, the license of every
  Maelys repository. The vendored `yyjson` keeps its MIT license. No code
  change.

## [0.1.0-alpha.1] - 2026-07-30

### Added

- First public alpha release of the bounded deterministic Datalog engine.
- Native C11 API and Python, JavaScript, WebAssembly, and WASI bindings.
- Semi-naive fixed-point evaluation with stratified negation.
- Bounded `SMALL` and `LARGE` memory profiles.
- Policy identity, diagnostics, proof records, and decision receipts.
- Public documentation, contribution guide, security policy, and CI workflow.

[Unreleased]: https://github.com/maelys-dev/maelys-datalog/compare/v0.1.0-alpha.3...HEAD
[0.1.0-alpha.3]: https://github.com/maelys-dev/maelys-datalog/releases/tag/v0.1.0-alpha.3
[0.1.0-alpha.2]: https://github.com/maelys-dev/maelys-datalog/releases/tag/v0.1.0-alpha.2
[0.1.0-alpha.1]: https://github.com/maelys-dev/maelys-datalog/releases/tag/v0.1.0-alpha.1
