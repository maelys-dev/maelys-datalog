# Sized reference sessions — inventory and proposed consumer contract

Status: proposal, 2026-09-27. No new API, capacity, memory mode or backend
capability is implemented by this document. The inventory describes the 0.12.0
engine at `e8bfd813789147daf02dbbe5b830df7158e0af41`. Header consolidation in
#130 does not change these structures. Implementation remains a separate
workstream after the window features and compatibility review; no release
version or projected byte count is promised.

This is a bounded public-reference proposal within existing SMALL/LARGE build
ceilings. It complements the broader [session resource design](session-resource-contract.md),
without freezing its backend negotiation, catalogue, elastic modes or strict
native no-heap artifact. Four explicit advanced capacities are considered here;
versioned predefined profiles remain a separate product decision.

The follow-on [0.14.0 capacity specification](../proposals/backend-session-resources.md)
records accepted option B: a distinct fixed backend resource contract and
explicit ABI 5 admission in 0.14.0, with the allocation service deferred to 0.15.0.
XLARGE remains a future additive compilation profile with a public-boundary
compatibility test required before the fixed-capacity delivery. The reference-only
first-delivery scope recorded below remains the historical proposal, not the
external-backend support matrix of that follow-on. Neither document implements
an API or changes program/build bounds.

## Reproduce the current inventory

From the repository root:

```sh
bash tools/session_storage_inventory.sh SMALL > /tmp/session-small.csv
bash tools/session_storage_inventory.sh LARGE > /tmp/session-large.csv
```

The tool compiles diagnostic translation units against the actual runtime and
solver definitions. It reports `sizeof`, `alignof`, member offsets, allocation
payload sums and profile limits. It does not execute or time a solve. Compiler
identity and Git revision/dirty state go to stderr; use `CC` to select another
native compiler. The probe
is not installed or linked into the SDK. Keep generated CSV outside git.

The observations below use Apple Clang 21.0.0 on macOS arm64, 8-byte pointers,
without `MAELYS_TESTING`; each of the three containing objects has alignment 8.
The existing whole-engine allocation guard independently reproduces the shared,
copied-policy and exporting totals in both profiles. These are requested engine
bytes, not allocator metadata, RSS, stack peaks or physical zeroing traffic.
Offsets do not establish absolute cache-line alignment; no timing conclusion
follows from this inventory. Other ABIs, including wasm32, need their own run.

## Current reservation, without double counting

A reference session sharing an engine-owned policy uses three allocations:

| Allocation | SMALL bytes | LARGE bytes |
| --- | ---: | ---: |
| Public session, including public result metadata | 1,664 | 1,664 |
| Prepared input state | 156,792 | 271,480 |
| Native result workspace, borrowing the inputs | 127,584 | 203,360 |
| **Total** | **286,040** | **476,504** |

The prepared input state breaks down as follows:

| Component | SMALL bytes | LARGE bytes | Dependency |
| --- | ---: | ---: | --- |
| Transaction dictionary | 40,976 | 40,976 | Text, entries, index and metadata |
| Native input facts | 73,728 | 147,456 | 72 bytes per input slot |
| EDB descriptor and pair-batch scratch | 9,296 | 17,488 | 1,104 fixed + 8 bytes per input slot |
| Symbol-sort / fact-insert scratch union | 32,768 | 65,536 | 32 bytes per input slot on this ABI |
| Remaining pointers and padding | 24 | 24 | Fixed in this layout |

The fact-insert index is only 4,096/8,192 bytes and shares that union; it must
not be added again. The dictionary consists of 32,768 text bytes, 6,144 bytes
for 512 entries (12 each), a 2,048-byte complete index and 16 bytes of metadata.
The input scratch counts symbol **occurrences**, up to four per raw submitted
fact; a small distinct-symbol budget alone cannot shrink it safely.

The native result workspace breaks down as follows:

| Component | SMALL bytes | LARGE bytes |
| --- | ---: | ---: |
| Derived native facts | 73,728 | 147,456 |
| Per-derived-fact proof indices | 2,048 | 4,096 |
| Input predicate ranges | 512 | 512 |
| Retained proof tree | 6,872 | 6,872 |
| Retained premise pool | 40,960 | 40,960 |
| Per-node premise metadata | 320 | 320 |
| Per-derivation witness scratch | 640 | 640 |
| Other metadata and padding | 2,504 | 2,504 |

The fixed provenance and witness subtotal is **48,792 bytes**, independent of EDB/IDB
capacity in the current representation. It supports the existing explanation
contract even when no optional explanation preparation workspace was requested.
Reducing it or disabling explanations is not part of this proposal.

### Other ownership and backend paths

| Host session path | SMALL bytes | LARGE bytes |
| --- | ---: | ---: |
| Reference, engine-owned policy shared | 286,040 | 476,504 |
| Exporting solve callback, engine-owned policy shared | 441,688 | 787,800 |
| Reference, caller-owned policy copied | 638,008 | 828,472 |
| Exporting solve callback, caller-owned policy copied | 793,656 | 1,139,768 |

The exporting path adds 80 bytes per input slot and 72 per derived slot in its
creation-time payload. Whether export is needed follows `borrows_inputs`, not
a backend name. These figures exclude any backend-owned state or supplied
backend arena; the checked exporting fixture requests no additional storage.

Caller-owned policies require a retained compiled snapshot of **351,968 bytes**
in the current layout. This preserves the ability to release and reuse policy
storage while sessions survive. Simply borrowing that arena would break the
lifetime contract. Engine-owned policy allocation is shared but still exists;
a bundle may retain other compiled policies until its final session closes.

Also excluded: the optional 456-byte configuration handle, policy loading,
caller input buffers, window banks and their second session, optional
explanation workspaces, backend/callback allocations, stack and Python/CFFI
objects. A session-only reduction is not a whole-application memory bound.

## Payload projections, not implemented storage requirements

Let E/D be input/derived slots, S the total symbol-entry capacity and T the
symbol-text bytes. Retain the complete 2 KiB symbol index and all existing
provenance, predicate metadata and other fixed fields. On the measured ABI,
substituting only the variable array payloads yields:

```text
56,664 + 112*E + 74*D + 12*S + T
```

The input coefficient is 72 for facts + 32 for the reused sort/index scratch
+ 8 for pair-batch scratch; the derived coefficient is 72 + 2. The full SMALL
and LARGE capacities reproduce their measured totals exactly.

| E | D | S | T bytes | Payload projection, shared policy |
| ---: | ---: | ---: | ---: | ---: |
| 16 | 32 | 32 | 2,048 | 63,256 bytes |
| 64 | 128 | 128 | 8,192 | 83,032 bytes |
| 256 | 256 | 256 | 16,384 | 123,736 bytes |

These are arithmetic projections retaining today's fixed bytes, **not measured
candidate sessions, supported profiles, upper bounds or a storage API**. Pointer/
capacity descriptors and new alignment padding are not modeled; different
index/arena designs can change the result. Program vocabulary must fit S/T,
and all program/profile restrictions still apply. The copied-policy path would
still add its snapshot. A session near a few KiB is not supported by this design
while retaining the existing provenance guarantee.

## Proposed consumer semantics

Keep the existing session handle and extend its configuration; do not create
another Python/native session abstraction. Public declarations and exact function
names are deliberately deferred until the contract review.

- With no capacity override, preserve current profile defaults and admission.
  A capacity request is explicit and immutable for the new session. Resolve it
  once, expose its effective values, and never clamp it or grow on exhaustion.
  Representation/index widths and program structural maxima remain build bounds.
- E bounds raw facts supplied to a session snapshot **before deduplication**,
  as well as its materialized input pool. It does not count compiled policy
  facts. A separately constructed input EDB or window has its own raw storage
  limits; its final submitted snapshot must also fit E.
- D bounds the complete derived relation, including non-query helpers. It is
  independent of E. Per-predicate limits and compiled policy-fact accounting
  remain as defined by the program/profile, not a new overloaded E/D meaning.
- S/T cover the complete transaction dictionary, including rooted program
  vocabulary and NUL-terminated text. Admission rejects a request below the
  program's existing count/used bytes. Existing canonical ID ordering, typed
  equality and snapshot lifetime remain unchanged. Keep the full symbol index
  initially; rebuilding or resizing it is a separately measured change.
- An absent override means defaults; an explicit zero must not also mean
  default. The proposed exact-capacity form permits zero E/D, or zero S/T for
  an empty program dictionary. An attempted insertion/derivation beyond zero
  fails normally. Final API encoding must distinguish these cases explicitly.
- Capacity exhaustion uses the existing explicit failure statuses, without a
  partial result or fallback allocation. Diagnostics identify the effective
  dimension and bound. `limit_get` continues to report build ceilings; a new
  effective-session accessor must not silently change that existing meaning.

A read-only, allocation-free requirements operation must compute checked bytes
and alignment for the actual policy, normalized capacities, backend and selected
explanation mode. Initialization in an exclusive aligned caller arena recomputes
or validates that plan, rejects short/overlapping storage before publication,
and clears the output handle on failure. Overlap checks cover the ranges known
to the call; exclusive ownership of unrelated caller arenas remains the caller's
obligation. Partial initialization is cleaned up
without allocating; arena contents may change on failure. The arena remains
immovable until session close, which does not free it.

The allocating convenience path uses the same plan. Its proposed target is one
session-arena allocation for the reference, including native result/input state;
this allocation count is not implemented yet. Optional separately supplied
storage and configuration/policy allocations remain explicitly accounted for.
Planning and initializing a session with an already prepared configuration is
not a claim that constructing that configuration or loading the policy avoids
allocation. Static build-time provisioning needs published target-specific upper
bounds/alignment in addition to a runtime query; it is not supplied by this CSV.

Preserve the current policy lifetime rules on both ownership paths. Preserve the
one-live-result lease through the last prepared explanation, canonical IDs and
explicit explanation truncation. No allocation may move into materialization,
solve, query, result release or the configured reference explanation path. All
rollback/preflight storage must fit the creation-time plan; existing byte-for-byte
failure contracts remain tested. Optional explanation preparation storage is
accounted separately from the retained provenance above.

Different normalized capacities must participate in execution identity, so two
sessions with different effective limits cannot be borrowed by one window as
matching sessions. Storage addresses and current occupancy are not identity.
The default-capacity path must retain its current fingerprint; the new encoding
for explicit non-default capacities needs a versioned, tested definition.

## Compatibility boundary with external backends

`program_info.max_input_facts`, `max_derived_facts` and
`max_facts_per_predicate` are **program/build representation bounds**. They must
not become session quotas. In particular, the existing 64/256 per-predicate
values remain unchanged. `storage_requirements(program)` and
`prepare(program, storage)` in backend ABI 5 do not receive a session resource
contract; keep their signatures, descriptor size and meaning intact.

The proposed first implementation supports non-default capacities and the new
caller-arena session constructor for the canonical reference backend only.
Unsupported backend/capacity/storage combinations are rejected before preparing
or publishing a session. Existing ABI 5 external-backend constructors, program
views, canonical snapshots, emission limits and commit/abort behavior remain
unchanged. Copied or wrapped descriptors must not accidentally inherit an
unadvertised sized-reference capability.

| Combination | Proposed admission |
| --- | --- |
| Existing constructor, default capacities, reference | Preserve existing behavior |
| Existing constructor, default capacities, external ABI 5 backend | Preserve existing behavior |
| Explicit smaller capacities, canonical reference | New planned/sized path after validation |
| Non-default capacities or new session-arena constructor, external ABI 5 backend | Reject as unsupported in the first delivery |

This preserves the existing extension contract rather than sending smaller
values through `program_info` and hoping a backend accepts them. It does not
promise smaller private backend storage. If a downstream consumer requires
sized external sessions immediately, revisit the scope and negotiate a distinct
resource contract with tested backends before implementation. No ABI number is
reserved here, and descriptor prefix compatibility is not assumed.

The installed-SDK external backend tests must pass unchanged on default paths.
Downstream integration suites still need replay against the candidate SDK;
public tests cannot certify an untested backend. New static-input/expiry window
operations need their own transactional oracle tests with such backends, even
if the descriptor ABI remains unchanged. This proposal changes no backend code. Requirements from a downstream
implementation enter this review as written proposals, never copied code.

Before the 0.13.0 release cut, install the integrated candidate SDK into a clean
prefix and build/replay the downstream consumer against that prefix. Record the
SDK and consumer revisions, profile, commands, results and exact source changes
required. The maintainer's include-only review of the header consolidation is
separate evidence and does not replace that final integration build.

## Implementation and acceptance sequence

1. Review this inventory and consumer contract, including the reference-only
   scope, exact zero semantics, execution identity and explanation provisioning.
   Approve the actual supported combinations before publishing declarations.
2. Replace fixed session arrays with checked arena slices, while preserving the
   profile ceilings and program representation. Audit every macro-based loop,
   copy/reset, union, index mask, static assertion and legacy copying path; a
   smaller allocation alone does not make the old accesses safe.
3. Verify the two ownership paths, full defaults and at least two smaller
   configurations coexisting in one process. Test zero/one/full/overflow limits,
   checked arithmetic, short/misaligned/overlapping arenas, constructor cleanup,
   duplicate/collision/wrap inputs, vocabulary rejection and reuse.
4. Retain allocator-disabled execution and configured explanations; test live
   result/explanation leases, late window rejection and complete rollback.
   Compare canonical facts/IDs and explanation/truncation results with the
   unchanged full-capacity reference for cases admitted by both contracts.
5. Measure actual reserved bytes, initialization/reset writes and scoped
   per-function software instructions before claiming a gain. Record changed
   layouts; use the established A/A and alternating A/B protocol for hot-path
   timing observations. Smaller storage alone does not establish faster solving.
6. Expose the same native configuration in Python and test the complete lifecycle,
   defaults, reuse and failures. Python conversion allocations remain separate.

The windows and this design can progress independently. The implementation of
sized sessions remains contingent on the inventory tradeoff and backend review;
the ARM64 timing diagnostic is a separate measurement follow-up.
