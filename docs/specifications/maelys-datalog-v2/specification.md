<!-- SPDX-License-Identifier: CC-BY-4.0 -->
# MAELYS-DATALOG-v2

Status: normative.

`MAELYS-DATALOG-v2` is the common version of three textual surfaces:

1. the source language accepted by the Maelys Datalog lexer and parser;
2. the canonical Why-true document emitted after solving;
3. the canonical Why-false document emitted after solving.

The token is interpreted by context. In a buffer manifest,
`"default_profile":"MAELYS-DATALOG-v2"` selects the source language. As the
first line of an output document, it identifies the global version and the
mandatory next line `document=why-true` or `document=why-false` identifies the
document kind. Readers must dispatch on both lines, not on the version alone.

The normative package is:

- [source-language.abnf](source-language.abnf): source bytes and grammar;
- [why-true-text.abnf](why-true-text.abnf): Why-true document bytes;
- [why-false-text.abnf](why-false-text.abnf): Why-false document bytes;
- [semantics.md](semantics.md): constraints not expressible in ABNF;
- [aggregates.md](aggregates.md): stratified aggregate semantics and compatibility;
- [conformance.md](conformance.md): executable conformance contract.

The ABNF uses RFC 5234 and RFC 7405 case-sensitive literals. The C engine is
the executable authority. A disagreement between this package and the engine
is a specification defect, never permission to guess or accept more syntax.

## Compatibility

Version 2 consolidates the already-published language: positive recursion,
typed comparisons, bounded integer arithmetic, isolated anonymous variable
`_`, stratified `not(atom)`, and contextual `or`. That consolidation added no
parser feature. The subsequent additive count extension introduces a new body
literal and explanation alternatives; existing source and output bytes retain
their previous interpretation. Consumers using exhaustive IR or explanation
kind switches must add aggregate support or reject unknown alternatives.
The integer `min`, `max` and `sum` extension added in 0.7.0 negotiates each operator
separately; `AGGREGATES` retains its count-only promise.

Historical v1 documents remain historical evidence. Buffer manifests using
`MAELYS-DATALOG-TEXT-v1` are rejected and must explicitly migrate to the v2
profile. File manifests whose separate profile is `enforce` are unaffected.

Why-true v2 replaces the v1 header with exactly:

```text
MAELYS-DATALOG-v2
document=why-true
```

All following semantic bytes retain the accepted v1 contract.

The Why-false harmonization in 0.4.0 replaces its historical
`MAELYS-DATALOG-WHY-FALSE-v1` first line with exactly:

```text
MAELYS-DATALOG-v2
document=why-false
```

All bytes starting at `status=` retain the historical Why-false contract.
This is an intentional text-serialization break, not a source-language or C ABI
change. There is no legacy-output switch. Policy, program and execution
fingerprints are unchanged: this change does not alter their inputs or the
reference backend's semantic identity. A hash of the emitted document itself
does change. Existing Why-true output is unchanged.

The common envelope does not make the two payloads interchangeable:

| Document | Meaning | Status values |
| --- | --- | --- |
| `why-true` | A retained derivation witness for an IDB fact | `complete`, `truncated`, `not-derived` |
| `why-false` | Obstacles encountered in a bounded search for an absent fact | `complete`, `truncated`, `not-applicable` |

Both can be truncated. Membership remains the authority on whether a fact is
present; an incomplete explanation does not change that answer. Why-false is
not the logical negation of a Why-true witness or an exhaustive proof of absence.
