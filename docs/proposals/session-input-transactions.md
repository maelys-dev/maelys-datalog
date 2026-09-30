# Retained session inputs: concrete fixed-storage contract

Stage 2 of the public input-transaction plan; based on the unmerged consumer
introspection change. This contract is recorded before the implementation.

## Public surface and admission

`maelys/datalog_transactions.h` introduces an opaque `session_inputs` attachment
to an otherwise ordinary session. `session_inputs_storage_requirements` and
`session_inputs_init` accept an explicit versioned options record and caller
storage. The attachment must be initialized before that session's first solve.
It owns no heap allocation. Its storage must outlive the attachment; free the
attachment before the session. The ordinary session constructor's allocation
policy is unchanged. There is no allocating attachment convenience constructor.

Options declare retained-fact capacity, raw addition and removal capacities,
and a fixed allowed symbol vocabulary. All are bounded by the loaded library;
retained capacity is also bounded by the session's input quota. Initialization
copies the vocabulary, including the compiled program's roots. Unknown symbols
in additions/replacements are rejected, explicitly: this first contract does
not promise a growing vocabulary. An otherwise valid removal containing a
symbol outside that vocabulary is an absent-fact no-op. Vocabulary reservation
does not consume result dictionary IDs or change canonical result enumeration.
The opt-in execution fingerprint hashes a distinct domain, the original session
fingerprint, three capacities and the sorted unique vocabulary with little-endian
64-bit length prefixes. Incarnation/generation are excluded. Different admitted
input contracts cannot silently pass a window's equal-fingerprint check; removing
the attachment restores the original fingerprint. Ordinary sessions are unchanged.

`session_inputs_base` returns two opaque 64-bit words: an incarnation unique
within the loaded library and a committed generation. `session_inputs_replace`
accepts a complete snapshot and `session_inputs_apply` accepts separate supplied
addition/removal arrays. Both require that exact base and return an ordinary
leased result. They return no partial result on failure. Successful publication,
including a set no-op, advances the generation once; rejection leaves it
unchanged. Exhaustion fails before work rather than wrapping. Bases from another
attachment, an old incarnation at the same address or an older generation fail.

While attached, ordinary public `session_solve` is refused: entry selection is
explicit. Existing window adapters use the replacement entry internally; their
two sessions retain independent generations and never receive a delta based on
the other bank. This is full replacement catch-up, not opaque provider copying.

## Semantics, publication and representation

Dynamic EDB is a typed set. Validate every raw record (including cancelled and
duplicate records) before composition. The addition/removal raw limits are
independent of the retained distinct-fact limit. Boolean inputs normalize before
equality. Absent removals and existing additions are no-ops; additions win when
the same fact is in both arrays. Compiled policy facts are not removable and
remain separate. Invalid predicates, arities, kinds and oversized text fail.

Convert only supplied records into the frozen private vocabulary, sort and
deduplicate each lot without allocation, then compose L in a provisional bank:
one removal pass, one duplicate-addition pass and one backwards merge after
capacity validation. There is no repeated per-record pool displacement and no
data-dependent B/L/T selection. Snapshot replacement bypasses delta composition.

The committed bank, dictionary and base are immutable until every fallible step
has succeeded. Materialize a generation-scoped canonical EDB from the provisional
native facts, including only used input symbols in lexical order after compiled
roots. ABI 5/6 providers still receive the complete canonical snapshot. Their
commit is paired with the host bank exchange; result destruction after an abort
does not commit inputs. A window may abandon after solve when the previous
result is explanation-leased: both sessions retain their previous input bases.
Complete IDB, canonical IDs and explanation lifetime remain the existing API's.

The attachment distinguishes committed payload from transient candidate/sort/
conversion scratch. Rejection preserves committed bytes; transient scratch is
not an application observation and may change. Caller storage may not alias
session/provider/explanation storage, live attachments or the supplied buffers.
No allocation or growth occurs during replace/apply, commit or abort. Host work
is bounded by declared capacities; the provider work budget is not a sandbox
or a claim that all host validation instructions are metered.

## Qualification

Use an independent typed-set oracle, seeded traces, stale/cross-incarnation
bases, raw/exact capacity tests, unknown vocabulary, duplicate/cancelled invalid
inputs, boolean normalization and canonical-ID comparisons. Include reference
and separately compiled ABI 5/6 providers, allocator-disabled execution, failure
then retry, explanation leases, both real window types (static facts, shared
facts and expiry) and their late aborts. Mutation controls must distinguish the
linear merge boundaries, add-wins, stale-base check and publication boundary.

Measure full host work and scoped composition separately against explicit
snapshot replacement on the same provider/compiler/profile. Count instructions
per function first, retaining canonicalization, snapshot export, commit, abort
and release. This API does not promise whole-request O(delta) or a latency gain.
Backend delta delivery is stage 3 and is not inferred from ABI 5/6 compatibility.
