<!-- SPDX-License-Identifier: CC-BY-4.0 -->
# Stratified aggregates

`count(Projected, predicate(Terms), Result)` is a contextual body literal.
It counts distinct typed values of `Projected` in the matching facts of a
fully materialized, strictly lower stratum. It is not a function term, filter,
or user callback. Existing ordinary predicates named `count` remain legal;
the nested atom distinguishes the aggregate form.

```datalog
errors(Id, Service) :- log(Id, Service, "error").
error_count(Service, N) :- service(Service), count(Id, errors(Id, Service), N).
alert(Service) :- error_count(Service, N), N >= 10.
```

The domain registers the ordinary predicates before parsing. Runtime events
are EDB facts supplied through the API. Give each occurrence its own ID to
count repeated events separately. The source language's existing variable
identity rules apply (the first uppercase letter identifies a named variable).

## Binding and grouping

- `Projected` and `Result` are distinct named variables. `Projected` must occur
  in the nested atom and must not occur outside aggregate-local scopes in the
  rule. Repeated occurrences inside its atom constrain the same value.
- Every other named variable in the nested atom is a grouping key and must be
  bound by an ordinary positive body atom. Anonymous `_` terms are local
  existential variables; they are never counted or exported.
- `Result` must not occur in its nested atom. It binds an integer and may be
  used in the head, comparisons, filters, negation, or ordinary positive atoms.
  If already bound, the literal succeeds exactly when the typed value equals
  the count. Multiple counts can share an output as an equality constraint.
- The grouping domain is explicit. A bound group with no matching value
  produces zero; absent groups are not invented. A global count needs no
  grouping atom. Auxiliary rules express joins or filters before aggregation.
- Count is set-based after typed projection, not the number of derivations,
  pivot visits, or input duplicates. Integer `1`, boolean `true` and symbol
  `"1"` are three different values.

## Evaluation and updates

The dependency graph includes a strict edge from an aggregate's source to
its head. Cycles containing such an edge are rejected, including cycles
through other predicates. Positive recursion can compute the source first.
Counts, ordinary rules and stratified negation can then be composed.

Evaluation uses bounded storage and no per-group allocation. Existing fact,
variable, stratum and result capacities remain effective. Capacity errors
fail the solve without publishing a partial result. Results remain immutable
snapshots with the existing one-live-result lease.

Each reached aggregate literal scans a source slice and sorts its matching
projected values. For `E` evaluations, cost is the sum of
`O(S_i + M_i log M_i)`, where `S_i` is the number of candidate facts scanned and
`M_i` the matching facts before projection deduplication. There is no per-group
cache: multiple body bindings or fixed-point iterations can revisit one group.
The simple one-evaluation-per-group case therefore costs a source scan and a
sort per group; several aggregates over the same relation repeat that work.
EDB scans use a predicate slice; IDB scans cover the frozen stratum (or the final
IDB snapshot for Why-false), and policy-fact scans cover the policy-fact store.
The per-predicate bound limits runtime/IDB matches, not the whole stratum scan;
raw policy matches have the separate rule-fact bound.

Projection scratch is 2 KiB SMALL / 4 KiB LARGE with 16-byte native terms. Its
lifetime ends before recursive rule traversal continues; it is not retained
as recursive state. These sizes describe this buffer, not total solver stack
usage. Future aggregates must account for repeated scans and sorting explicitly.

This feature initially recomputes each supplied snapshot. It introduces no
stream, window, delta API or incremental backend. A later update that changes
`error_count("api", 1)` to `error_count("api", 2)` must retract the former
tuple and insert the latter atomically. Expiration has the same semantics as
deletion. Full recomputation is the conformance oracle for that maintenance.

## Extension compatibility

`AGGREGATES` promises this stratified distinct-count contract. Future operators
require separate capability/version negotiation;
they must not silently expand the promise made by existing providers.
Backends without `AGGREGATES` reject
aggregate programs before preparation. The public IR adds a count kind using
the existing `atom`, `lhs` (projected variable) and `rhs` (output variable)
fields; it does not enlarge descriptors, rules or arrays. Other fields are
inactive. Projection and output use variable IDs 0..25; other source IDs in
that range are bound group keys. Source IDs 26..31 are local existential
variables. Local IDs cannot occur in the surrounding rule; frontends assign
different IDs to independent anonymous terms within an atom. Backend ABI 3 and program ABI 1 layouts remain unchanged. Providers
must advertise only capabilities they implement and reject unknown kinds.
The historical `CAP_LANGUAGE` mask remains unchanged; aggregates are opt-in.

Existing programs keep their identities, result semantics and explanation
bytes. The new kind participates in normalized program identities. No
aggregate implementation is supplied through the string-filter SDK.

## Explanations

A Why-true `kind=count` premise records the source store, the instantiated group
pattern, projected variable and observed distinct count. Remaining `?N` terms
are local variables, not unknown grouping keys. This is an observation of the
frozen snapshot, like a negated-absence premise; it does not enumerate or prove
every member of the source relation. `parent=-` also applies to an IDB count.
The caller-owned premise layout and capacity do not grow. Existing explanations
without counts retain their bytes and truncation rules.

Why-false recomputes the bounded aggregate against the immutable result. A
bound result that disagrees produces `count-mismatch` with `observed`, typed
`expected`, projection and source pattern. Downstream comparisons continue to
produce comparison obstacles. These diagnostics inherit the existing bounded
search contract; they do not claim exhaustive proof of absence.

Planner SDK v1 callbacks continue to see only the existing literal kinds. The
core schedules a ready count itself before asking the planner for the next
ordinary literal. The planner cannot bypass binding or stratification checks.

## Integer extrema and sum (unreleased)

`min(V, source(...), N)`, `max(V, source(...), N)` and
`sum(V, source(...), N)` use exactly the same contextual syntax, local projection,
positive-bound grouping keys and strict stratification as `count`. Ordinary
predicates named `min`, `max` and `sum` remain legal. Their results use the
existing nonnegative integer domain, 0..2147483647 inclusive. There is no
floating-point, signed-integer, symbol ordering or boolean coercion.

| Operator | Matching source | Bound group with no matches |
| --- | --- | --- |
| `count` | Distinct typed projected values | Integer zero |
| `min` | Least projected integer | Literal fails; no result tuple |
| `max` | Greatest projected integer | Literal fails; no result tuple |
| `sum` | Projected integer from each distinct **complete source fact** | Integer zero |

Only matched source facts are type-checked. A matching non-integer for a numeric
operator, or a sum above 2147483647, aborts the solve with `INVALID_FIELD` and no
published result. A later valid snapshot may reuse the session. These errors do
not mean an empty group or a failed output comparison. Aggregates are evaluated
when reached by the rule evaluator; they are not a global column-type validator.

Two facts `sample(1,"api",10)` and `sample(2,"api",10)` contribute 20 to
`sum(V,sample(_,"api",V),N)`. Repeating the first complete fact contributes
nothing more. Different derivations of one IDB fact and repeated policy-fact
clauses likewise contribute only once. This is relation-set semantics, not a
bag of derivations and not `sum(distinct V)`. Retain event IDs in an auxiliary
relation if distinct occurrences must survive projection. To deliberately sum
distinct values, first derive a relation containing only the group and value.

```datalog
minimum(G, N) :- service(G), min(V, sample(_, G, V), N).
maximum(G, N) :- service(G), max(V, sample(_, G, V), N).
total(G, N) :- service(G), sum(V, sample(_, G, V), N).
```

Absent groups are never invented. Global `min`/`max` over an empty source also
fail; global `sum` returns zero. A prebound result is a typed equality constraint,
including when several operators share that result variable. An empty extremum
fails even if its output is unbound; it never fabricates a sentinel value.

The reference evaluator scans each source once for extrema, `O(S)`. Sum sorts
bounded pointers to matching complete facts and deduplicates them before checked
addition, `O(S + M log M)`. Numeric evaluation reserves at most
`max(MAX_RULE_FACTS, MAX_FACTS_PER_PRED)` fact pointers on its stack (1 KiB SMALL /
2 KiB LARGE on 64-bit targets); its frame returns before rule recursion. There
are no new ruleset/result fields, persistent indexes, per-group caches or engine
allocator calls. Each reached aggregate still repeats its scan; these operators
do not provide streaming or incremental maintenance.

The independent public capabilities are `CAP_MIN`, `CAP_MAX`, `CAP_SUM` (bits
9, 10, 11), also exposed as Python-next `Capability.MIN`, `.MAX`, `.SUM`.
`CAP_AGGREGATES` continues to promise **only count**; `CAP_LANGUAGE` is unchanged.
A provider missing any required operator is rejected before preparation, even
if it supports count. `CAP_ALL` includes all known bits and becomes 4095.

Public IR adds `IR_MIN=6`, `IR_MAX=7`, `IR_SUM=8` using the same active fields as
`IR_COUNT`; descriptor layouts and program ABI 1/backend ABI 3 remain unchanged.
Native literal/premise enums append the corresponding alternatives. The historical
`as.count` premise member stores all four aggregates without changing union size.
Consumers with exhaustive switches must handle or explicitly reject the new kinds.
Existing programs retain their normalized identities and explanation bytes.

Why-true uses `kind=min`, `kind=max`, `kind=sum` with the same frozen-snapshot
pattern, projection, value and `parent=-` contract as count. Why-false reports
`min-mismatch`, `max-mismatch`, `sum-mismatch` for a different bound output, or
`min-empty`/`max-empty` with projection and pattern but no observed/expected pair.
Native obstacle kinds append values 7..11 in that order. These alternatives also
work with caller-owned or configured explanation storage and preserve leases,
short-output retry and bounded-search behavior. They extend the existing text
formats additively; a reader must accept or explicitly reject unknown alternatives.
