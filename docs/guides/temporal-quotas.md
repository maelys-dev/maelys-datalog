# Temporal quotas for SDK consumers

This guide describes the **published v0.21.0 contract**, not a new API. It uses
one quota key and unit-cost actions. Git Core must choose its own horizon,
identity, concurrency and durability rules before adopting the example.

## Authorization is not a solve status

`OK` means a complete Datalog result was accepted. A derived `deny` is an
ordinary result fact; it does not abort a session solve or window push. The
application checks its decision predicates and handles errors by refusing the
operation. It must not interpret an absent decision as permission.

A temporal window is still last-N: `push_until` can evict the oldest event at
capacity **even if that event's deadline is in the future**. Insertion never
expires other events implicitly. `expire(now)` explicitly removes deadlines
`<= now`, preserves static input, and commits a monotone watermark. It can fail;
a failed expiry is not permission to forget the due events. A successful no-op
expiry advances time without replacing the result. See
[`datalog_window.h`](../../include/maelys/datalog_window.h).

Consequently, neither successful publication nor `WINDOW_EXPIRATION` provides
"keep the entire live horizon or refuse". Eviction can silently undercount a
quota even though the storage/window transaction itself is correct.

## A safe consumer pattern

[`examples/temporal_quota.c`](../../examples/temporal_quota.c) separates:

1. **Performed events:** the application's authoritative bounded ledger, with
   distinct occurrence IDs and deadlines. Only actual actions are recorded.
2. **Proposed event:** `proposed(0)` is an evaluation input, not a performed event.
3. **Authorization:** solve with the ledger's current counter or its complete
   unexpired history, require `allow(0)` and absence of `deny(0)`, then release
   the result. A denied or failed action adds no performed event.

The policy admits one additional unit only when the current count is below two:

```datalog
limit(2).
used(N) :- counter(N).
used(N) :- history_mode(0), count(I,event(I),N).
deny(0) :- proposed(0), used(N), limit(L), N >= L.
allow(0) :- proposed(0), used(N), not(deny(0)).
```

Supply **either** `counter(N)` **or** `history_mode(0)` plus `event(ID)` facts,
never both representations. Count projects occurrence IDs, not repeated action
payloads: `count` is distinct-value counting. Counter maintenance, including
expiration, is consumer work; an integer alone does not implement a sliding
horizon. The fixture retains the same event/deadline ledger for both injected
variants to make that cost visible.

Serialize expire/check/reserve/execute/record per quota key. Reserve a ledger
slot before executing; refuse when no slot is available, rather than dropping
an unexpired event. Concurrent/asynchronous actions need pending reservations
counted during later checks, and a durable completion/reconciliation protocol.
The synchronous example models one successful or failed action without a
process crash. It is not a distributed transaction or a Git operation journal.
Use an authoritative time source and monotone expiry; define whether quotas
count performed actions, accepted attempts or reservations, rather than mixing
them. The example uses the literal deadline 100 only as a deterministic test.

A window can instead hold performed events, but its current FIFO contract
requires a consumer capacity check and a proven bound on the entire horizon.
Do not push a proposal as an actual event to discover permission afterwards.
For this simple policy a current window result can describe the next unit's
permission, but publication after the external action can still fail. Recovery
and durable accounting remain the consumer's responsibility. Arbitrary policy
changes, proposed action parameters and external side effects require a more
complete protocol; two window banks alone do not supply one.

The example checks failed actions, refusal without losing live events, exact
expiry boundaries, counter/history agreement, the loaded per-predicate boundary
and reuse after rejection. Its deliberately unsafe FIFO witness confirms that a
third push into a two-slot window publishes `deny`, evicts unexpired ID 0 and
retains only two events. This witness documents behavior, not recommended usage.

## Storage inventory, including the reservations

The example was compiled separately against clean installed static SDKs from
v0.21.0 (`9630f591637c7046ec73996458e1ba282194e32c`), macOS ARM64,
Apple Clang 21.0.0 (clang-2100.1.1.101), CMake Release, on 2026-10-08. These are queried reservations,
**not RSS, allocation peaks, latency or a Git Core performance result**.

All variants load the identical policy above and use the canonical reference
backend. E=2 for the counter, E=N+2 for history/window, D=4, S=32, T=1024 bytes.
Input buffers reserve 256 bytes of interned text each. Window options reserve two
static slots and expiration metadata; N is 64 in SMALL and 256 in LARGE.

| Profile | Representation | Per-session host | Sessions | Input/adapter | Consumer ledger | Total without explanation workspace |
| --- | --- | ---: | ---: | ---: | ---: | ---: |
| SMALL | Counter injected | 93,872 | 1 | 520 | 1,024 | 95,416 |
| SMALL | History injected | 96,432 | 1 | 6,664 | 1,024 | 104,120 |
| SMALL | Two-session window | 96,432 | 2 | 15,488 | 0 | 208,352 |
| LARGE | Counter injected | 106,160 | 1 | 520 | 4,096 | 110,776 |
| LARGE | History injected | 116,464 | 1 | 22,024 | 4,096 | 142,584 |
| LARGE | Two-session window | 116,464 | 2 | 52,352 | 0 | 285,280 |

Units are bytes. The ledger reserves N records of two uint64 values (ID and
deadline). For the window, occurrence/deadline storage is already in the adapter;
zero in the ledger column does **not** mean no external reservation journal or
recovery state is needed in a real authorization integration.

The host component includes reference native workspace/scratch, dictionary,
input materialization and reserved results/provenance; this reference plan
reports backend=0, padding=0, and both external slices=0. These zeros do not mean
the reference solver uses no storage: its reservation is accounted in host.
The adapter includes **both** input/text banks and its scratch/metadata. Optional
prepared Why-true/Why-false workspace is not silently charged to the default:

| Profile | Per-session optional explanation reservation | Counter total | History total | Window total (two workspaces) |
| --- | ---: | ---: | ---: | ---: |
| SMALL | 116,088 | 211,504 | 220,208 | 440,528 |
| LARGE | 148,856 | 259,632 | 291,440 | 582,992 |

Both explanation kinds are enabled in these rows; explanation output text is
still consumer storage. The shared caller-owned policy storage requirement is
2,787,624 bytes, alignment 8, in both builds; add it **once** per shared policy
storage range, not once per session. It is a fixed profile bound, not the size
of the source string. Config objects, application stacks, allocator metadata,
bindings, durable journal and shared executable pages are outside these totals.
No SDK storage plan measures complete process memory or stack high-water usage.

For another policy/configuration, query the plan again. Sum
`host_bytes + backend_bytes + explanation_bytes + padding_bytes = arena_bytes`,
then add external slices exactly once using `total_execution_bytes`. Add the
input/adapter requirements and separately declared consumer storage. Caller-owned
memory must satisfy the returned size/alignment and remain exclusive. E counts
raw inputs before deduplication; D includes all IDB helpers; S/T include program
symbol roots. Compiled facts also contribute to native EDB reservations.

The build still limits a predicate to **64 facts in SMALL / 256 in LARGE**;
smaller session quotas do not increase that bound. Raw event capacity, group
contribution capacity, distinct fact count and aggregate count are different
quantities. For example, duplicate group contributions can occupy event slots
without creating distinct source facts. Query the loaded limits/program info;
do not infer feasibility from a text budget or from global E alone.

The internal fact reduction from 72 to 40 bytes is one component of the plan.
It cannot establish the memory saving of this consumer's full integration.

## Current execution contracts in one place

| Contract | What is implemented | What remains work for the host/provider/consumer |
| --- | --- | --- |
| Sized sessions | Explicit E/D/S/T, normalized admission and queried caller-owned plans; reference prepared results/provenance reserved at initialization | Build/program limits remain separate. Quotas do not resize the language or give a backend new capabilities. Explanation workspace is opt-in. |
| Backend ABI 5 | Complete input snapshot, prepared provider storage, complete IDB emission and commit/abort lifecycle | No normalized per-session quota negotiation. Whole-session plan/init rejects explicit ABI 5; supported legacy configured creation remains separate. |
| Backend ABI 6 | Snapshot delivery plus normalized session resources in sizing/prepare | The provider chooses its algorithm and must explicitly admit the resources; default reference solving recalculates. |
| Backend ABI 7 | Explicit REPLACE/DELTA packets and committed base tokens, with ABI 6 resource negotiation and complete IDB output | No automatic ABI upgrade. Retained-input attachment is required; provider owns incremental maintenance. Complete output is still required. |
| Window catch-up for ABI 7 | Delta against **that session bank's** committed base; facts supported by another event/static EDB remain present; rejection does not advance its base | The host still constructs/materializes the complete candidate set. No whole-request O(delta) guarantee. |
| Public reference backend | Snapshot solver, retained-input convenience and both window adapters usable | Input deltas do not make reference derivation incremental. Time, quota interpretation, authorization, concurrency and action accounting remain consumer duties. |
| Optional backend allocation | Explicit opted-in provider growth under the host's charged ceiling, strict provisional ownership rollback | Not automatic host/input growth or a process-memory limit. Custom backend/filter allocation guarantees remain their own. |

See [the installed resource records](../../include/maelys/datalog_resources.h)
and [ABI 7 input delivery](../proposals/backend-input-delivery.md). Historical
proposal texts describe earlier stages and must not be mistaken for this table.

## Reproduce the fixture against an installed SDK

Build/install the v0.21.0 SDK with the selected CMake profile, then:

```sh
cc -std=c11 -Wall -Wextra -Werror -I"$SDK/include" \
  examples/temporal_quota.c "$SDK/lib/libmaelys_datalog.a" -o /tmp/temporal-quota
/tmp/temporal-quota > /tmp/temporal-quota.csv
```

Assertions must remain enabled. Compile SMALL and LARGE consumers separately;
the installed library determines limits. Raw CSV/build logs stay outside Git.
The repository's CMake test `temporal_quota_consumer_contract` runs the same
contract checks; no timing is collected. The example prints every plan component
and the optional workspace variants rather than relying on a sizeof estimate.

The accepted v0.21.0 Python reports
[37092737587](https://github.com/maelys-dev/maelys-datalog/actions/runs/37092737587)
and [37097581473](https://github.com/maelys-dev/maelys-datalog/actions/runs/37097581473)
retain their slowdowns, classifications and David's decisions in the
[release review](../validation/v0.21-release-review.md). This guide adds no timing
exception and establishes no new performance claim. A Git Core claim needs its
representative complete authorization/action-accounting path and named evidence;
a timing gap alone justifies no engine correction.

The missing opt-in horizon retention behavior is proposed separately in
[window horizon retention](../proposals/window-horizon-retention.md). No new API
or automatic `deny` veto is implemented here.
