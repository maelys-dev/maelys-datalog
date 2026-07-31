# MAELYS-DATALOG-v2 conformance

Conformance is executable and fail-closed.

1. `tools/validate_maelys_datalog_v2_spec.py` validates both ABNF files,
   rejects malformed grammar structure, and resolves every rule reference.
2. `tests/test_maelys_datalog_language_spec.c` checks the active profile,
   parses representative valid and invalid source through the real C parser,
   validates manifest profile migration, canonical hash stability, and all
   three Why-true document states.
3. `tests/test_maelys_datalog_corpus.c` replays the versioned `.dl` corpus.
4. Native, Python, WASM, and sanitizer suites replay every boundary whose
   output header or canonical hash is affected.

The ABNF validator is not a Datalog implementation and has no semantic
authority. Passing ABNF validation cannot override a disagreement with the C
lexer/parser or formatter.

## Coverage map

| Contract family | Positive witness | Negative witness |
|---|---|---|
| facts/rules/UTF-8/comments | `examples/language.dl` | corpus malformed fixtures |
| wildcard `_` | `examples/language.dl` | `invalid/wildcard-head.dl` |
| canonical `not(...)` | `examples/language.dl` | `invalid/not-without-parens.dl` |
| contextual `or` | `examples/language.dl` | `invalid/uppercase-or.dl` |
| arithmetic/typed comparison | `examples/language.dl` | existing arithmetic corpus |
| Why-true complete | formatter golden | structural formatter negatives |
| Why-true truncated/not-derived | formatter goldens | structural formatter negatives |
| manifest profile | v2 manifest test | v1 and unknown profile tests |

Every new fixture is also exercised from `tests/corpus/v2/` by the engine's
real parser and diagnostic machinery.
