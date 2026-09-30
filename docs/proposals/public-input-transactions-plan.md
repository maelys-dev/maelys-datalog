# Public engine input transactions — delivery plan

Status: implementation authorized on 2026-09-30, based on public engine
`58b326c7000519dfd0bf0abe268d5ab597316da5` (v0.15.0). Each stage has its own
change branch, isolated worktree, signed commits and reviewable pull request.
Merge and release decisions remain separate from implementation authorization.

## Scope and sequence

1. **Consumer introspection.** Extend scalar build-limit queries with policy
   atom count/text bounds. Expose normalized program counts through the opaque
   policy consumer API and through Python and the shared JavaScript/TypeScript
   binding. Keep loaded-library ceilings, immutable program counts and effective
   session capacities distinct. Read-only queries allocate no engine storage and
   do not change admission, solver behavior, fingerprints or backend ABIs.
2. **Transactional retained input.** Add explicit snapshot replacement and
   supplied add/remove operations against a committed input generation. Use L's
   bounded linear composition, with additions winning, raw validation before
   deduplication, no partial publication, no allocation on the fixed execution
   path, and an independent typed-set oracle. Existing backends receive complete
   snapshots. Qualify actual sessions and both window banks, including leases,
   expiry, rejection and retry; the modeled window of #140 is not this proof.
3. **Backend delta delivery.** Specify and implement a separately negotiated
   input transaction contract. Keep ABI 5/6 snapshot providers compatible. No
   implicit fallback or guessed descriptor tail. A public conformance provider
   demonstrates initialization, current-base validation, commit/abort, complete
   output and explanation leases; no private incremental implementation enters
   this repository. Delta delivery must avoid compulsory full input conversion
   before the callback, and its remaining host work stays in the measurements.

The public reference solver may recompute the complete IDB. Incremental
derivation/support maintenance belongs to external providers. Optional backend
allocation, elastic host storage, XLARGE and #145's sorting experiment are not
part of these three stages. Version assignment follows qualification; older
proposals naming the allocator as a 0.15.0 delivery are superseded by the
published 0.15.0 changelog, which defers it.

## Evidence and completion

For each stage, preserve the base/head identities and compatibility matrix;
run meaningful C and installed-SDK tests in SMALL/LARGE, C11/C++ consumers,
allocation guards and ASan/UBSan where execution changes. Maintain binding
parity and native/WASM validation when their surfaces change. Invalid arguments,
unsupported selectors/contracts and short records must fail without partial
output. Input changes additionally need seeded differential traces, exact
capacity/reuse/rollback tests and negative controls with a passing baseline.

The transaction contract inherits the guarantees and explicit scope limits in
[backend transaction deltas](backend-transaction-deltas.md). Before production
code, record concrete public declarations, ownership/storage planning, raw batch
limits, vocabulary lifetime, stale-base behavior and bank catch-up. Compatibility
must be demonstrated using separately compiled providers and callers.

Keep #140's original four-path experiment and unsuccessful T criterion. Adopt
L for the delta entry, retain explicit snapshot replacement, and do not choose a
runtime threshold. Measure whole host and provider scopes separately, instruction
counts per function first. Retain copies, resets, canonicalization, complete IDB
publication, commit/abort and release. Run timing only with the repository's
declared controls; no whole-request O(delta) or latency gain follows from a
software-instruction reduction. Generated evidence stays in artifacts, not Git.

Before any release, obtain and review the complete Python performance report on
the final runtime/binding/build candidate. The accepted v0.15.0 report does not
qualify these changes. No merge, release publication or private-backend change
is authorized by this plan.

## Progress

- Stage 1: PR #151, signed `fb8c41bf508525c7e785a843a925546bc7a787e3`,
  ready for review after all 31 checks of run 36709791297 passed.
- Stage 2: implemented on `feat/session-input-transactions`, stacked on #151.
  Thirty-two suites pass in SMALL/LARGE and under ASan/UBSan; 15 rebuilt
  mutations are detected in each profile. The instruction experiment compares
  three public entries on the same SDK, not different solver revisions.
- Stage 3: queued after retained input; separate descriptor negotiation review.
- #140 is an existing, unmerged experiment. Its measured algorithm decision is
  usable design evidence; this plan does not merge it or modify its old report.
