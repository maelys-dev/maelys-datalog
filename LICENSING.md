# Licensing

Copyright 2026 Maelys Developers.

The socle wrote this file once because it was missing. It belongs to this
repository now: state below what each part is licensed under, and name every
document this repository engages publicly. `bin/maelys-platform docs` reads
this file to tell a public engagement from prose that migrates elsewhere.

## Source code: MPL-2.0

The source code of maelys-datalog is available under the Mozilla Public License
2.0. The complete terms are in [`LICENSE`](LICENSE).

The MPL applies file by file. A program that links this code, statically or
otherwise, keeps its own license (section 3.3 of the MPL); only a modified
covered file must remain available in Source Code Form under MPL-2.0.

## Installed agent texts: CC0-1.0

The managed blocks of `AGENTS.md` and `CLAUDE.md` are installed from the
`share/` texts of the Maelys distributions, which are dedicated to the public
domain under CC0-1.0. They carry no license obligation of their own.

## SDK templates: MIT

The copyable extension starters under `sdk/templates/` carry
`SPDX-License-Identifier: MIT` file by file, deliberately: they exist to be
copied into a project of any license, so the MPL's file-by-file obligation
would defeat their purpose. Every other source file of this repository,
including the working examples under `sdk/examples/`, stays MPL-2.0.

## Redistributed material

- **yyjson** (MIT), vendored in `vendor/yyjson/` and compiled into the
  library, so it travels inside every released artifact. Its notice is
  redistributed as `licenses/yyjson/LICENSE` in the native tarballs and in
  the `@maelys-dev/datalog-wasm` package.

Nothing else is linked in: the engine has no third-party runtime dependency.

## Documents engaged publicly

Shipped inside every released artifact by `scripts/package-release.sh`:

- [`LICENSE`](LICENSE) and `licenses/yyjson/LICENSE`
- [`CHANGELOG.md`](CHANGELOG.md)

Referenced from [`README.md`](README.md), the repository's public entry point:

- [`docs/specifications/maelys-datalog-v2/specification.md`](docs/specifications/maelys-datalog-v2/specification.md),
  with the `semantics.md`, `conformance.md`, ABNF grammars and `.dl` fixtures
  beside it: they are the normative definition of the language this engine
  implements, and third-party frontends and backends are written against them.
- [`docs/architecture/open-core.md`](docs/architecture/open-core.md),
  [`docs/architecture/compiler-backends.md`](docs/architecture/compiler-backends.md)
  and [`docs/architecture/extension-contexts.md`](docs/architecture/extension-contexts.md):
  the extension contracts an SDK consumer needs.
- [`docs/repository-history.md`](docs/repository-history.md)
- [`SECURITY.md`](SECURITY.md) and [`RELEASING.md`](RELEASING.md)

Not engaged, and free to migrate: `docs/release-engineering.md`,
`docs/validation.md` and `docs/extension-sdk-validation.md` — internal records
of how this repository is released and validated, which nothing published
points at.
