# MAELYS-DATALOG-v2

Status: normative.

`MAELYS-DATALOG-v2` is the indivisible version of two textual surfaces:

1. the source language accepted by the Maelys Datalog lexer and parser;
2. the canonical Why-true document emitted after solving.

The token is interpreted by context. In a buffer manifest,
`"default_profile":"MAELYS-DATALOG-v2"` selects the source language. As the
first line of an output document, it identifies the global version and the
mandatory next line `document=why-true` identifies the document kind.

The normative package is:

- [source-language.abnf](source-language.abnf): source bytes and grammar;
- [why-true-text.abnf](why-true-text.abnf): Why-true document bytes;
- [semantics.md](semantics.md): constraints not expressible in ABNF;
- [conformance.md](conformance.md): executable conformance contract.

The ABNF uses RFC 5234 and RFC 7405 case-sensitive literals. The C engine is
the executable authority. A disagreement between this package and the engine
is a specification defect, never permission to guess or accept more syntax.

## Compatibility

Version 2 consolidates the already-published language: positive recursion,
typed comparisons, bounded integer arithmetic, isolated anonymous variable
`_`, stratified `not(atom)`, and contextual `or`. It adds no parser feature.

Historical v1 documents remain historical evidence. Buffer manifests using
`MAELYS-DATALOG-TEXT-v1` are rejected and must explicitly migrate to the v2
profile. File manifests whose separate profile is `enforce` are unaffected.

Why-true v2 replaces the v1 header with exactly:

```text
MAELYS-DATALOG-v2
document=why-true
```

All following semantic bytes retain the accepted v1 contract.
