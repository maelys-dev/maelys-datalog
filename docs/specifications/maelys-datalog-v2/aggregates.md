<!-- SPDX-License-Identifier: CC-BY-4.0 -->
# Stratified distinct count

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

This feature initially recomputes each supplied snapshot. It introduces no
stream, window, delta API or incremental backend. A later update that changes
`error_count("api", 1)` to `error_count("api", 2)` must retract the former
tuple and insert the latter atomically. Expiration has the same semantics as
deletion. Full recomputation is the conformance oracle for that maintenance.

## Extension compatibility

`AGGREGATES` promises this stratified distinct-count contract. Future operators
such as `min`, `max` or `sum` require separate capability/version negotiation;
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
