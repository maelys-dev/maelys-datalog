<!-- SPDX-License-Identifier: CC-BY-4.0 -->
# MAELYS-DATALOG-v2 semantics

This document is normative where ABNF cannot express registry, typing, graph,
capacity, and canonical-order constraints.

## Source program

- The predicate registry is closed before parsing. Every predicate and exact
  arity must exist. EDB and POLICY_FACT predicates are forbidden in rule heads.
- A direct fact must use a POLICY_FACT predicate and be ground.
- Head variables, comparison variables, arithmetic variables, and variables in
  `not(...)` must be bound by positive body atoms as required by the parser.
- `starts_with(Value, Pattern)`, `ends_with(Value, Pattern)`, and
  `contains(Value, Pattern)` are contextual FILTER body literals only when the
  name is absent from the predicate registry. A registered homonym remains an
  ordinary atom. `Value` is a symbol or a variable bound by a positive body
  atom; `Pattern` is a source string constant. A filter is non-generative,
  forbidden in facts and heads, and creates no stratification dependency.
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

## Ground string filters

FILTER literals compare UTF-8 bytes exactly, without locale or Unicode
normalization. `starts_with` and `ends_with` test the corresponding byte edge;
`contains` tests for a contiguous byte subsequence. Matching is case-sensitive
and an empty pattern matches every symbol. The pattern belongs to policy code:
it is stored in a fixed ruleset-owned POD pool and is neither interned as a
symbol nor checked against the atom whitelist.

The reference filter table provides three standard filters. Its identities are
`string.starts-with.utf8-bytes-v1`, `string.ends-with.utf8-bytes-v1`, and
`string.contains.utf8-bytes-v1`. Filter name, semantic identity, symbolic value
term, and exact source pattern bytes contribute to the canonical ruleset
fingerprint. A filter program contains no pointer, callback, destructor, or
runtime ownership.

An embedding may register additional filters through the versioned module SDK,
before initializing any ruleset. Registration seals at the first ruleset
initialization; callbacks and semantic identities cannot be replaced afterwards.
Names use the `predicate` grammar. Declared domain predicates retain precedence;
an otherwise unknown name is rejected. A provider validates its constant pattern
before a filter literal commits any program/pattern state. This is an opt-in
extension of the reference language, not a guarantee of arbitrary regex support.
The [module contract](../../architecture/open-core.md) defines callback,
lifetime, identity, budget and distribution boundaries.

The manifest `sha256` still verifies the original source bytes. After successful
parsing, a policy using an external filter or planner receives a canonical
executable identity incorporating used filter semantics and the selected planner
name/semantic ID. Standard policies preserve their prior identities. Registration
order and unused external filters do not change executable fingerprints. Numeric
filter IDs are process-local implementation details, not portable serialized IDs.

Each admitted evaluation is charged before matching. `starts_with` and
`ends_with` cost the pattern byte length. `contains` costs
`max(1,value_bytes) * max(1,pattern_bytes)`, with checked multiplication. A
solve admits at most `MAELYS_DATALOG_MAX_FILTER_EVALUATIONS` evaluations and
`MAELYS_DATALOG_MAX_FILTER_COST_UNITS` cost units. Invalid state, type, overflow,
or an exceeded bound fails closed and never becomes an ordinary non-match.
Why-false uses its independent
`MAELYS_DATALOG_MAX_WHY_FALSE_FILTER_COST_UNITS` bound and reports truncation
instead of changing the already-finalized solve.

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
`not(...)`, comparisons, and successful filters become typed premise records.
FILTER premises retain their lexical body index and expose public name,
semantic identity, ground value, and source pattern. These rules bind the
source and output surfaces to the same global v2.
