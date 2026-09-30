# Backend input delivery: explicit ABI 7 contract

Stage 3, based on the unmerged retained-input PR #152 at
`1b57788956bc9c3897246e73806bea1d5c23841e`. This contract precedes its code.

## Negotiation and ownership

`maelys/datalog_backend_transactions.h` defines a separately typed ABI 7
descriptor and an explicit configuration setter. ABI 5/6 descriptors, registries
and snapshot callbacks are unchanged. No language capability means delta input;
no guessed descriptor tail, name-based upgrade or fallback is permitted. This
first revision uses direct configuration, not an extension-registry ABI change.

ABI 7 retains ABI 6's normalized fixed resources and storage/prepare contracts,
and ABI 5's complete emission, explanations, commit, abort and destroy contracts.
Its solve callback instead accepts a versioned input transaction. Configuration
copies the descriptor and identity; provider code must outlive the session.
Execution fingerprints distinguish this delivery contract even when provider
names, semantic IDs, resources and program identities equal a snapshot provider.
An ABI 7 session requires a retained-input attachment before solving.

## Transaction packet

The packet names REPLACE or DELTA, its previous and next input bases, and two
bounded opaque views. REPLACE's additions are the complete dynamic EDB and its
removals are empty. DELTA applies removals first, then additions; absent removals
and existing additions are no-ops. The host may suppress redundant additions.
Compiled facts are obtained from the immutable program, never from these views.
Facts are validated, deduplicated and typed; booleans are normalized. Their order
is an internal set order, not promised lexical or canonical-ID order.

An accessor exports one fact from a view into caller storage. All descriptors,
views and returned text are borrowed for the synchronous solve callback only.
Providers copy values/text they retain into their own reserved storage. There
are no public symbol IDs in an input packet. This avoids compulsory full public
snapshot conversion before a delta callback; the host still materializes native
canonical EDB for output validation, queries and explanations. Whole-request
O(delta) and zero canonicalization are expressly not promised.

The first packet has an empty committed base at generation zero. Providers must
stage from that base, check later bases against their own committed token, and
adopt the next token only in commit. A successful empty delta advances the token.
After callback or host failure, destroy_result aborts; the old base survives.
Windows deliver replacement catch-up against each bank's own committed base.
An explanation lease can reject a window after solve: neither provider nor host
may publish that candidate. A replacement does not grant arbitrary rebasing.

Output remains the complete IDB, including helpers, with existing sticky emit,
filter and work errors. Explanation state lives through the last explanation
lease. Commit is infallible and allocation-free. No private derivation algorithm
or capability assertion is introduced by the host delivery mechanism.

## Proof and measurement

Compile the public conformance provider separately against an installed SDK;
compare typed results/canonical IDs with fresh reference snapshots over seeded
updates, replacement, rejection/retry and both real window adapters. Verify
provider base/commit/abort, aliases, exact capacities, absent vocabulary,
callback failures, sticky output errors, explanation leases and allocator guards.
Keep ABI 5/6 consumers unchanged and test explicit wrong-version/short/missing
callback rejection. Rebuild negative controls with passing baselines.

Compare the same conformance algorithm receiving snapshots or transactions in
one instruction experiment. Preserve full host/provider lifecycle counts,
per-function repetitions, attribution residuals, CPU/compiler/binary provenance
and checked complete output. Avoid timing conclusions from software counts.
The public reference remains a snapshot solver; private incremental maintenance
and optional allocation stay outside this change. Merge/release require their
separate approvals and the final runtime needs its new Python report.
