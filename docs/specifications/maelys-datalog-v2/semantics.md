# MAELYS-DATALOG-v2 semantics

This document is normative where ABNF cannot express registry, typing, graph,
capacity, and canonical-order constraints.

## Source program

- The predicate registry is closed before parsing. Every predicate and exact
  arity must exist. EDB and POLICY_FACT predicates are forbidden in rule heads.
- A direct fact must use a POLICY_FACT predicate and be ground.
- Head variables, comparison variables, arithmetic variables, and variables in
  `not(...)` must be bound by positive body atoms as required by the parser.
- Symbols and booleans admit only `=` and `!=`; ordered comparisons and
  arithmetic operate on integers. Arithmetic is bounded to signed engine
  integer semantics and rejects overflow.
- Positive recursion is allowed. Negation must be stratifiable, all its
  variables must already be positively bound, and recursion through negation
  is rejected.
- `_` is a fresh parser-local variable at every occurrence. It is never
  interned and is forbidden in heads, direct facts, comparisons, arithmetic,
  and `not(...)`.
- Contextual `or` joins positive atoms only. Comma-delimited prefix and suffix
  literals are common to every alternative. Cartesian expansion follows
  lexical order; every expanded rule is validated independently and the whole
  clause is atomic on failure. A predicate named `or` remains legal where an
  atom is expected.
- The hard limits in `maelys_datalog_types.h` are normative: token and string
  bytes, symbols, predicates, arity, rules, rule facts, body literals, named
  and total rule variables, arithmetic nodes/depth, strata, and profile fact
  capacities. Decimal source integers are non-negative and at most
  `MAELYS_DATALOG_MAX_INT`.
- Parsing is atomic: a failing clause leaves no partially expanded rule.

Whitespace is ASCII space, tab, CR, or LF. Line comments start with `%` and
end at LF or EOF. Block comments are not nested. Source strings contain valid
UTF-8 bytes between quotes and have no escape language; a quote or backslash
therefore cannot be encoded inside a source string.

`not`, `true`, and `false` are reserved lexical forms. `or` is deliberately
not reserved globally: it is an operator only in the parser context defined
above and remains a legal predicate name where an atom is expected.

## Why-true document

The document describes a finalized, ground, derived IDB witness. Its status is
`complete`, `truncated`, or `not-derived`. Counts equal emitted elements,
indices are contiguous from zero, and `result-step` is the last complete step.
IDB parents refer to an earlier valid step; other parents are `-`. Steps are
ancestors-first and premises retain lexical body order.

Output uses LF only and ends with one LF before the terminating NUL supplied by
the C API. Valid UTF-8 symbols preserve non-ASCII bytes and use named escapes
for quote, backslash, LF, CR, and tab; other controls use uppercase `\xHH`.
For an invalid UTF-8 symbol, non-printable bytes use uppercase `\xHH`.

The document is ground: it contains no variable or `_`. Source `or` has been
expanded before solving and never appears as an explanation operator.
`not(...)` and comparisons become typed premise records. These rules bind the
source and output surfaces to the same global v2.
