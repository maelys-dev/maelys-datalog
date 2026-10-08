# Proposal: refuse full temporal windows instead of evicting live events

Status: proposal only, 2026-10-08. No declaration, flag or behavior below is
implemented. Existing v0.21.0 FIFO behavior remains unchanged.

## Need and scope

Authorization quotas must retain every event counted inside their horizon, or
refuse the proposed operation. The current last-N adapters evict at capacity
even when the oldest deadline is in the future. Explicit expiration does not
change that rule. A consumer can prevent eviction with serialized bookkeeping,
but the adapter cannot currently enforce this invariant itself.

Propose an opt-in creation flag, with a final public name decided during API
review, for **both** single-event and group windows. It selects refusal instead
of FIFO eviction when the event/group slot limit has been reached. Default
constructors and options without the flag keep their current semantics. This
would extend the accepted options flags, not the backend ABI or Datalog syntax.
No automatic clock, expiry, storage growth or `deny` predicate convention is added.

## Transaction requirements

- The caller explicitly expires due events first. This is a separate existing
  transaction: if it commits, a subsequent rejected push does not undo it.
- A full slot ring refuses a push before solving, allocating through a provider
  or modifying committed state. Proposed status: `PAYLOAD_TOO_LARGE`, matching a
  bounded capacity refusal, with the final diagnostic specified in API review.
- Facts, text, result/views, next occurrence/group ID and the watermark remain
  unchanged by this refused push. Output IDs remain untouched. No event,
  including an untimed event, is implicitly retired to make room.
- Slot limits apply even to empty groups or duplicate contributions. Raw
  contribution, distinct fact, per-predicate, static and text limits retain
  their own independent checks and failure semantics.
- Freed slots after successful explicit expiry may be reused; IDs do not wrap
  or recycle. If expiry fails, its events remain accounted and the caller must
  refuse or recover. A horizon with more live events than reserved slots fails
  closed, rather than silently becoming a smaller horizon.
- Result/explanation leases and ABI 5/6 snapshots or ABI 7 bank bases retain
  their existing commit/abort guarantees. No successful publication is reversed
  merely because the result contains `deny`.

## Qualification before any implementation claim

Test legacy eviction unchanged; refusal while the oldest event is still live;
all-full/all-expired/partially-expired rings; equality at deadline; untimed and
out-of-order deadlines; empty groups and duplicate/shared/static contributions;
raw/text/per-predicate exhaustion; ID boundaries; and rejection/retry under live
explanation leases. Verify committed state/outputs and provider bases after
refusal, with allocator counters and a recording provider proving no solve.

Rebuilt negative controls must detect accidental FIFO eviction, publication or
ID consumption on full refusal, and counting distinct facts instead of slots.
An installed-SDK consumer must exercise both adapters. Measure full host/provider
instructions and reservations only if a performance claim is proposed; changing
capacity refusal is primarily a safety contract, not a speed optimization.

## What this does not solve

This protects retention, not authorization/action atomicity. A window push is
still a successful Datalog transaction even when `deny` is derived. Applications
must distinguish proposals, permission and performed events, and coordinate
concurrent reservations and external action failures/crashes. See
[the temporal quota guide](../guides/temporal-quotas.md).

A generic prepare/evaluate/accept-or-abort window API would be a separate,
larger proposal with ownership, leases and external-action recovery defined.
It is not necessary to document a safe consumer-owned ledger today, and is not
implicitly included in this flag. No database dependency is proposed.
