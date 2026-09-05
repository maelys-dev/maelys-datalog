# Changelog

All notable changes to Maelys Datalog are documented in this file.

The project follows [Semantic Versioning](https://semver.org/) and uses the
format described by [Keep a Changelog](https://keepachangelog.com/).

## [Unreleased]

### Added

- Versioned public C SDK for separately compiled string-filter and join-planner
  modules, with bounded startup registration and immutable module identities.
- Installed-SDK consumer tests, static/shared integration tests, callback-error
  and budget checks, and cross-process semantic fingerprint tests.

### Changed

- Standard string filters now live in `modules/standard/` and use the same SDK
  as external modules; the default behavior and standard fingerprints remain.
- Extended policies bind filter/planner semantics into their executable identity
  after source integrity verification. Module failures remain fail-closed.
- All build variants use shared source manifests. Public CMake include paths no
  longer expose private engine headers; native packages include the module SDK.

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
