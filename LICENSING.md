# Licensing

Copyright 2026 David Bromberg.

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

## Installed agent texts: CC-BY-4.0

The managed `maelys-release` blocks of `AGENTS.md` and `CLAUDE.md`, and
`.claude/skills/maelys-release/SKILL.md`, are installed by
`maelys-release adopt` from the maelys-release distribution. Its
`share/agents/` texts are licensed under CC-BY-4.0 with attribution to
David Bromberg since maelys-release v0.28.0. Blocks installed before that
version were copied under CC0-1.0 and that grant stands for those copies;
the notices identifying copyright, source and license arrive with the next
adoption. Retain them, and indicate your changes when sharing an
adaptation. This applies to the installed blocks, not to what this
repository writes outside them.

## The MAELYS-DATALOG-v2 specification: CC-BY-4.0, with its code components MIT

`docs/specifications/maelys-datalog-v2/` is normative and exists to be
implemented by third parties, so it does not follow the engine's license. It
had none of its own until now and fell back to MPL-2.0 by default, which is a
source-code copyleft: it would not have stopped anyone from implementing the
specification, but it would have made quoting it — in another document, an
article, a derived specification — a licensing question, for a text whose whole
purpose is to be followed.

The prose is **CC-BY-4.0**: quote, translate and redistribute it, with
attribution.

- [`specification.md`](docs/specifications/maelys-datalog-v2/specification.md),
  [`semantics.md`](docs/specifications/maelys-datalog-v2/semantics.md),
  [`conformance.md`](docs/specifications/maelys-datalog-v2/conformance.md)

Its code components are **MIT**, because an implementer copies them into a
parser generator or a test suite, and Creative Commons licenses are not meant
for software:

- the ABNF grammars `source-language.abnf` and `why-true-text.abnf`, which
  carry the identifier in a `;` comment;
- the `.dl` corpus under `examples/` and `invalid/`, which carries none: the
  conformance runner reads the first line of each fixture for its `EXPECT:`
  marker, so a header there would change what the test sees. They are MIT by
  this statement and by their path.

Changing the specification's license removes the patent grant MPL-2.0 carried
over its text. The engine's own code stays MPL-2.0, with that grant intact.

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

- The normative definition of the language this engine implements, which
  third-party frontends and backends are written against. The specification
  links the other two, so they are engaged with it and must be named here to
  stay beside it:
  [`docs/specifications/maelys-datalog-v2/specification.md`](docs/specifications/maelys-datalog-v2/specification.md),
  [`docs/specifications/maelys-datalog-v2/semantics.md`](docs/specifications/maelys-datalog-v2/semantics.md),
  [`docs/specifications/maelys-datalog-v2/conformance.md`](docs/specifications/maelys-datalog-v2/conformance.md),
  with the ABNF grammars and `.dl` fixtures beside them.
- [`docs/architecture/open-core.md`](docs/architecture/open-core.md),
  [`docs/architecture/compiler-backends.md`](docs/architecture/compiler-backends.md)
  and [`docs/architecture/extension-contexts.md`](docs/architecture/extension-contexts.md):
  the extension contracts an SDK consumer needs.
- [`docs/repository-history.md`](docs/repository-history.md)
- [`SECURITY.md`](SECURITY.md) and [`RELEASING.md`](RELEASING.md)

Referenced from material this repository ships or runs, which makes them
engaged even though no reader arrives at them from the README:

- [`docs/validation.md`](docs/validation.md) — named in
  [`CHANGELOG.md`](CHANGELOG.md), which travels inside every released tarball,
  and in [`bindings/wasm/README.md`](bindings/wasm/README.md). A reader of a
  shipped changelog must find it here.
- [`docs/release-engineering.md`](docs/release-engineering.md) — cited by
  [`scripts/package-release.sh`](scripts/package-release.sh), which builds
  every artifact and points at D1, D2, D3 and D7 for the target matrix, the
  emsdk pin, the receipts and the socle; by `scripts/release-gates.sh`; and
  by [`RELEASING.md`](RELEASING.md), the operator's sequence. The scripts
  say what they say for reasons a migration tool cannot rewrite.

Nothing else in `docs/` is unengaged: `maelys-platform docs --prose` reports
no document to migrate.
