# Last-N event window

`<maelys/datalog_window.h>` adds a native C adapter above the public snapshot
API. It retains the last **N successfully accepted events**, in arrival order,
and recomputes the whole snapshot on every push. It neither changes Datalog
syntax nor adds an incremental backend. Backend ABI 3 and program ABI 1 remain
unchanged. Python and JavaScript bindings do not yet expose this adapter.

## Event model

One push supplies a predicate and zero to three typed payload values. The adapter
prepends a generated, nonnegative integer occurrence ID, making one EDB fact.
For example, pushing `event` with the values `0, 5` three times produces
`event(0,0,5)`, `event(1,0,5)`, `event(2,0,5)` when the initial ID is zero.
The registered predicate must have the resulting arity, here three.

The existing language can then express:

```datalog
group(0).
occurrences(G,N) :- group(G), count(I,event(I,G,_),N).
values(G,N) :- group(G), count(V,event(_,G,V),N).
total(G,N) :- group(G), sum(V,event(_,G,V),N).
smallest(G,N) :- group(G), min(V,event(_,G,V),N).
largest(G,N) :- group(G), max(V,event(_,G,V),N).
```

The three repeated payloads yield an occurrence count of three, a distinct value
count of one, and a sum of fifteen. `group` must allow policy facts; each derived
predicate must be declared as IDB (and QUERY if the application queries it).
Empty groups retain the normal aggregate rules: count/sum yield zero and
min/max do not match. Negative values and overflow retain the integer aggregate
contract; a failed evaluation rejects the entire push.

N is shared across all event predicates. At N=3, `[A,B,C] + D` proposes
`[B,C,D]`. A rejected D leaves `[A,B,C]` and its result intact. The initial
window is empty but already has a solved result, including any policy facts.
Static context can be expressed with policy facts; a separate mutable static
EDB, multi-fact events, explicit deletions, batches, clocks, timestamps and
out-of-order delivery are outside this initial API.

IDs advance only on success. After committing ID `INT32_MAX`, pushes fail with
`PAYLOAD_TOO_LARGE`; the reported next ID is `INT32_MAX+1`. IDs never wrap or
reuse a ring position. `first_occurrence` permits an application to start from
a persisted cursor, but this is not checkpoint restoration or durable delivery.
The application must advance its own input acknowledgement only after success.

## Storage and ownership

1. Create two distinct, idle sessions of the same policy/backend/options/profile.
   The adapter checks equal execution fingerprints. Reference sessions are the
   supported baseline; custom providers still owe their own ABI contracts.
2. Query `window_storage_requirements(N, text_capacity, ...)`, reserve that many
   aligned bytes, and call `window_init` with the two sessions. The query covers
   the adapter and **two** N-entry input buffers, each with `text_capacity` bytes
   for interned predicate/symbol strings. It excludes both sessions, their
   result/provenance reservations, optional explanation workspaces, and stack.
3. The adapter borrows exclusive use of both sessions and the arena until
   `window_free` succeeds. Never solve/free/share these sessions independently
   during that interval. Configuration is completed before initialization;
   borrowed explanation arenas must be separate for the two sessions.
4. Push one event, check the status, then obtain the borrowed result with
   `window_result`. Use the usual query, enumeration and explanation APIs.
   **Never call `result_free` on this borrowed result.** A successful push or
   window destruction invalidates it; a failed push preserves it and its views.
5. Release prepared explanation handles before advancing/destroying the window.
   Otherwise publication/destruction returns `INVALID_STATE`. A push may have
   completed candidate work before discovering this lease. Free the window,
   then free the two sessions and release application storage.

A successful `window_free` marks the handle closed and clears its borrowed
references. While the caller arena remains alive and unmodified, subsequent
operations with valid arguments, including a second free, return `INVALID_STATE`
without touching the sessions; the sessions may already have been destroyed.
The arena can be passed to `window_init` for a new lifetime. This guard cannot
protect a dangling arena pointer or distinguish old handles after the same
storage is reused. Release, repurposing or reinitialization ends the old handle
and view lifetimes.

`window_events` exposes the committed, ordered input with generated IDs.
`input_edb_view` is the underlying read-only buffer view; it also supports copying
entries to a different input buffer. Neither view permits mutation or aliasing
the source into a mutating operation on itself.

`window_state` reports the committed event count and next occurrence ID.
`window_text_usage` reports the committed input bank's used bytes and total text
capacity in constant time, via the public `input_edb_text_usage` accessor. Used bytes
include the terminating NUL for each interned predicate or symbol; capacity is
the configured per-bank total, not the remaining space. The count can remain
below N when text is exhausted. Repeated strings share storage and expiry can
recover it on a successful push. Rejected candidates do not change either
reported value. This is not total engine memory or a peak measurement: candidate
scratch, indexes, session vocabulary and policy storage are excluded. Remaining
text space does not guarantee admission against the engine's other bounds.

All handle access must be serialized, including queries and explanations.
Scalar outputs, input strings and workspaces must respect the disjoint-storage
rules in the headers. Reentry during a window operation returns `INVALID_STATE`.

## Transaction and memory guarantees

The current input/result occupies one bank. The other bank is scratch: clear
it, copy the retained suffix, append the proposed event, then solve using its
session. Only after success and release of the old result lease does the adapter
swap banks and advance the cursor. Nothing that follows old-result release can
fail. Candidate results are released when publication is blocked.
If releasing the current result fails, its actual status is preserved and the
diagnostic reports that release failed and the candidate was discarded; it does
not infer a particular cause from the status alone.

Rejected input, text/symbol/fact/derivation saturation, aggregate overflow or a
backend failure preserves the committed bank, result and cursor. Scratch and
the candidate session may change. This is an atomic commit of engine state, not
rollback of application callbacks' external side effects. Initialization can
also fail while evaluating an empty runtime EDB; no adapter result is left leased.

The adapter performs no allocator calls, including initialization/destruction.
With already created reference sessions, push/query/result replacement and
prepared explanations retain the reference engine's execution allocation
guarantees. Session creation, policy compilation, legacy allocating explanation
calls, and third-party providers remain separate. This does **not** certify a
whole-lifecycle zero-heap engine or transitive library allocation behavior.

N may be at most the loaded library's `MAX_EDB_FACTS`. It is an event storage
capacity, **not** a promise that every policy and payload fits: per-predicate,
symbol/text, derived-fact, depth and explanation bounds still apply. No implicit
growth, fallback allocation or smaller window is used on exhaustion.

Strings are interned in each input bank. Rebuilding the reference snapshot resets
its runtime vocabulary to the prepared policy, so expired-only strings do not
accumulate indefinitely. A future persistent backend must supply its own
vocabulary contract; this reference behavior does not prove reclamation in it.

The memory cost deliberately includes two full sessions. This first adapter is
the transactional reference for subsequent delta maintenance; it does not claim
incremental performance or dimension sessions according to N. SMALL/LARGE remain
whole-engine build profiles. Larger profiles, elastic storage, per-session
resource negotiation and ABI 4 are separate work.

## Validation

`test_maelys_datalog_window` compares every IDB relation, canonical IDs and
resolved symbol text with an independently maintained FIFO and snapshot oracle.
It covers aggregate expiry, negation, bounded recursion, duplicates, generated
sequences, a negative oracle control, ID exhaustion, input/predicate/text limits,
initialization failure, backend faults before work and after result emission,
prepared leases, and vocabulary rotation beyond the cumulative symbol capacity.
Lifecycle checks close the window, destroy both sessions, and exercise every
closed-handle entry point while retaining the arena, including under ASan/UBSan.
They also reinitialize that arena for a new lifetime. Text checks cover shared
strings, saturation below N, unchanged usage on rejection and expiry reclamation.
The same consumer runs against the installed static and shared SDK.

`test_maelys_datalog_window_alloc` instruments all engine allocation paths, disables
them after session creation, and checks init/push/query/configured and prepared
explanations/replacement/free. On rejection it compares the entire committed
input bank and window metadata byte-for-byte, deliberately excluding candidate
scratch. Both tests run in SMALL/LARGE and under ASan/UBSan. These are correctness
and allocation checks, not a timing comparison or performance claim.
The allocation guard also covers closed-handle rejection, text observation and
same-arena reuse. A test-only release failure verifies the neutral diagnostic,
candidate cleanup and successful retry without attributing every failure to a
prepared explanation.
