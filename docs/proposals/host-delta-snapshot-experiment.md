# Bounded host delta → snapshot experiment

Status: test-driver experiment following agreement on
[the engine response](backend-transaction-deltas.md), merged as #138 (`98898dc`).
There is no installed delta function, descriptor field, capability or reserved
ABI version. The production engine and its ABI 5 snapshot path are unchanged.
A host instruction saving is necessary evidence for further design review, not
permission to publish a delta API.

## What this first experiment admits

`bench/host_delta.c` includes the unchanged runtime translation unit and redirects
only its materialization call in this diagnostic binary. A calls the production
snapshot materializer. B validates changed facts into bounded scratch, copies the
committed native EDB into staging, applies removals then additions, finalizes the
EDB, and uses the **same production canonical export, backend invocation, emitted
result validation, deferred commit and release** as A. B copies the accepted EDB
back into retained storage; both O(N) copies and the complete export are counted.
No second snapshot materialization is hidden outside B's scope.

Both modes use the same ABI 5 provider, compiled separately against a clean
installed SDK. It records the entire canonical input and implements a complete
one-rule projection with three distinct variables. A projection from an empty
predicate supplies the inert/empty-IDB case. The other projects the input facts.
Unrelated policy facts declare a fixed vocabulary. Domain `atoms` merely authorize
source literals: they do not themselves preload the compiled dictionary.
Backend text hashing, recording, projection and receipts are included in total
work. This provider is a semantic fixture, not a representative application
solver or evidence of a private backend's performance.

B accepts only program-preinterned symbols. An unknown addition is rejected;
unknown removals are validated and treated as absent without interning. This
avoids vocabulary churn and preserves the snapshot's canonical symbol IDs. The
prototype is intentionally not a general public-input replacement.

Each bank has its own committed EDB, numeric transaction generation and logical
trace position. A two-entry journal catches an alternating bank up from **its
own** base. There is no diff of a freshly generated snapshot in B. Accepted empty
updates advance the bank generation. Failed attempts leave the complete declared
retained arena and backend committed receipt unchanged. Working EDB, conversion
arrays and provider recordings are scratch, not retained state. This diagnostic
uses live bank addresses as ownership identities; safe token identity across
session destruction/recreation remains an API-design obligation.

## Matrix, scopes and exclusions

There are 40 ordinary fixtures in SMALL (8/64) and 60 in LARGE (8/64/256):
integer or fixed-symbol payloads, inert or projection provider, and empty / one /
four / complete replacement / alternating-bank traces. SMALL's per-predicate
bound is 64; its 256 fixture is explicitly excluded. Each fixture has eight accepted
transactions. Initial seeding, the first operation and the remaining seven operations
have separate count dumps. Eight is fixed before the final run; an earlier 32-step
SMALL smoke was retained and the incomplete LARGE smoke stopped to bound collection
cost. No timing/noise or candidate-dependent threshold chose this length. Inputs alternate forward/reverse snapshot order.

The window trace is a **model of two alternating banks**, not a modification of
the production window adapters. N is the number of raw occurrences: initially
two occurrences supply each of N/2 distinct facts. First/last supplier accounting
and the two-step bank catch-up are included. Do not describe its N=256 fixture as
a live base of 256 distinct facts. Actual static EDB, groups, deadlines, watermark
and occurrence-ID exhaustion are outside this first experiment.

Two complete, independent processes measure each scope:

- `engine`: submitted input through validation, export, solve, acceptance and
  result release; caller ledger/packing is outside this region.
- `caller`: the same work plus occurrence-ledger maintenance, production of the
  full snapshot for A or changed-fact journal for B, and bank selection/composition.
  The logical sequence of occurrence updates is pre-generated for both. This is
  the modeled caller lifecycle, not Python or a production window benchmark.

Six additional engine fixtures (two programs × three errors, 64 symbolic facts)
measure eight repeated backend failures, sticky work-budget failures, or injected
late acceptance rejections. Every failed attempt is checked and followed by
successful reuse. The late rejection simulates the final fallible boundary;
it is **not** a real prepared-explanation lease test. No explanation capability
is advertised by this provider.

An independent oracle reconstructs raw occurrences directly from the immutable
trace and calls the reference snapshot session after every accepted operation.
It compares exact canonical callback input count/order/types/values/text, complete
native EDB, IDB enumeration order and values, and canonical public symbol IDs/text.
Checks and reference solves are outside all counters. Native allocator traps
remain active for candidate operations, oracle queries, commit/abort and release.
The proof also exercises capacity overflow, raw batch overflow, invalid cancelling
entries, duplicate/add-wins updates, unknown symbols, stale/wrong-owner tokens,
live-result refusal, poisoned scratch reuse and generation exhaustion. Four
mutants must fail: wrong owner, skipped bank catch-up, publication on rejection,
and ignored raw validation failure.

Not yet exercised: a general indexed delta store, adversarial hash chains,
complete source-origin/window transactions, real explanation leases, recursion,
negation, aggregate changes, vocabulary reclamation, initialization failure and
cross-incarnation tokens. The broader conformance plan in the agreed note remains
required before a deliverable API. This bounded fixture does not satisfy it by
implication. B's insertion uses sorted-array moves; large/full replacements may
lose badly and must remain in the report.

## Counting and storage accounting

Run `sh bench/run_host_delta.sh /absolute/fresh/evidence` on Linux with Clang,
CMake, make, Python 3 and Valgrind installed. Both profiles and installed SDKs are
built first. Software counts then run in A1/B1/B2/A2 order, in separate processes,
for each scope. No wall-clock measurements are taken. Local Docker execution is
local instruction evidence, not a hosted timing run or a self-hosted ARM runner.

`report_host_delta.py` verifies the full inventory, semantic receipts and exact
Ir/Dr/Dw repetition for each operation **and every exclusive function**. Init
variation is retained separately. Inclusive call-cost records are never summed
as exclusive work. Undefined provider symbols are restricted to the SDK accessors
and host emission/charge services: their work belongs to the host, even when
called by the provider. The provider's own exclusive functions form its bucket;
every other exclusive function belongs to the host/caller/driver bucket. The
client-request boundary residual is preserved separately and reconciled with
the complete operation. It is not silently credited as host savings.

Function buckets identify input/transaction code, export, output services,
commit/release and caller production. Shared helpers and inlined runtime work
remain explicitly shared; these buckets do not pretend to separate every
semantic phase. Host saving is `(exclusive_host_A-exclusive_host_B)/host_A`;
complete-operation saving and backend work are separate. Ir is a software count;
Dr/Dw count memory accesses, not bytes, cycles or hardware misses. The incidental
simulated cache events in Callgrind files are not used to claim a cache mechanism.

Receipts report allocated request bytes/calls during diagnostic setup, not peak
RSS or the size of a proposed API. The prototype deliberately over-reserves the
same scaffolding for A and B, including two banks even for ordinary sessions.
On the measured 64-bit targets, each bank adds 36,888 bytes of retained EDB/token storage and 147,488 bytes of
conversion arrays/counts; the two-entry raw journal adds 163,872 bytes. The
recording provider and the existing external session have their own reservations.
These are experimental costs, not a storage optimization. Stack trace/oracle
arrays, compiler stack frames, shared policy storage and allocator overhead are
separate; no claim about complete peak/live process memory is made.

`observe_host_delta_memory.sh OUT SDK PROFILE` builds a separate observer that
counts bytes requested by explicit host memcpy/memmove/memset calls in the same
regions. It must run **after** instruction collection, never replace it, and its
semantic receipts must match. These are requested bytes, excluding compiler-made
struct copies and implicit stores, not total hardware traffic. Instructions come
only from the unobserved binary. Retain first/steady/init/failure byte records;
do not omit copying/zeroing or late-abort work because no allocation occurs.

Run `python3 -m unittest discover -s bench -p test_report_host_delta.py` for the
reporter and `python3 bench/check_host_delta_mutations.py BUILD --profile PROFILE`
against the uninstrumented driver. ASan/UBSan and memory scribbling runs complement
the disabled-allocator checks; none establish speed.

Keep raw count dumps, binary and SDK hashes, source manifest, provider disassembly,
receipts, memory observations and report SHA-256 in the evidence directory, outside
git. Put the concrete evidence location and results in the experiment PR. Preserve
losses and unmeasured cases. No instruction result authorizes a release or a new
ABI by itself; any eventual Python claim still needs the complete lifecycle
protocol.
