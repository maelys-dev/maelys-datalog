# Fixed session resources: implementation and acceptance

This is step 2 of the [reviewed 0.14.0 contract](../proposals/backend-session-resources.md),
based on `9338993f5eb82e928ebec99c385bef3df546c67f`. It does not declare 0.14.0
release qualification complete. The default ABI 5 declaration and callbacks stay
available; the distinct ABI 6 descriptor is in `<maelys/datalog_resources.h>`.

## Admission and ownership

Requests normalize E/D/S/T once per plan. Missing fields take loaded-profile
defaults, zero is exact, above-profile values are rejected, and compiled symbol
roots must fit S/T. The program's build/per-predicate bounds are unchanged.
Both ABI 6 requirements and preparation receive the same normalized record.
Explicit ABI 5 selections reject non-default quotas or an explicit resource
feature requirement; they are never migrated by matching names or callbacks.
The reserved allocator bit and elastic mode are refused before provider calls.

Caller construction recomputes the plan and checks arithmetic, alignment,
short buffers, overlaps with its arguments and known live session ranges.
Reservations are recorded before provider preparation, so reentrant creation
cannot reuse an arena still being initialized. The registry reads immutable
planned ranges, without following concurrently initialized input pointers.
It allocates and frees no engine storage. The convenience resource constructor
uses exactly one arena allocation. Program storage loaded in a caller buffer
is copied into that arena; an owned policy is retained instead. External
provider and explanation storage count once, by required slice length.

The native input pool reserves `min(B, E + F)` facts, where B is the build EDB
bound and F is the compiled fact count. F does not consume the raw dynamic
input quota E. Sort scratch uses raw term occurrences, not S. Native IDB and
proof indices reserve D entries; external result emission has the same D bound,
including auxiliary relations. Export storage is reserved only when the solve
callback needs it. There is no growth or allocator fallback after construction.

This implementation retains a fixed storage floor: the complete dictionary and
hash index, EDB insertion/pair scratch, and provenance structures still use
build-profile reservations. E/D payloads are sized. S/T are exact admission
bounds but reducing them does **not** currently shrink dictionary bytes.
The returned plan includes this floor, padding, policy retention/copy and the
selected explanation storage; it is not the earlier proposal's payload estimate.
`tools/session_storage_inventory.sh SMALL` and `LARGE` report actual layouts
and default reservations. The resource test prints plans for a smaller vector.

## Behavioral evidence

`test_maelys_datalog_session_resources` covers versioned prefixes/tails,
reserved-feature refusal, a recording ABI 6 provider, two coexisting vectors,
unchanged program bounds, default identity, exact zero bounds, raw duplicates,
auxiliary IDB accounting, rooted symbols, distinct E/D/S/T diagnostics, retry,
checked plans, external arenas, live overlap, prepare failure, both policy
ownership paths, typed registry rollback and wrong-ABI name refusal. A group
window rejects unequal resource fingerprints and preserves its committed result
on a late explanation-lease rejection with equal quotas.

The allocation-guard build disables engine allocation throughout caller arena
initialization, solve, query, explanation preparation/release and caller close.
It separately checks the allocating constructor's one allocation/one free.
Existing dictionary rollback, collision, canonical-ID, result-reset and window
tests remain in the native and sanitizer inventories; their budgets are not
relaxed. Python fixed-capacity tests run against installed SDKs, with default
constructor/call-budget and fingerprint tests retained. Python/CFFI still allocate.

`tools/check_session_resource_mutations.py --profile SMALL --output /tmp/resources-small`
(and `LARGE`) builds a passing ASan/UBSan baseline, then independently recompiles
each altered translation unit. Build failures are not detections. The witnesses
reject quota smuggling through `program_info`, accepted elastic/reserved features,
ABI 5 non-default admission, altered preparation resources, missing F reservation,
ignored S/T/D quotas, unchanged identity for changed quotas, missed live overlap,
unchecked arithmetic and commit after rejection. CI retains logs and results as
artifacts. An early missing-F mutant survived a result-only test because compiled
facts can be read directly by the solver; a storage-plan assertion now detects
that incorrect reservation as well. Preserve both runs when reviewing evidence.

## Remaining release qualification

Step 3 still requires the full separately compiled old/new caller/provider
matrix, the synthetic above-LARGE public-boundary fixture, downstream installed
SDK replay, and measured reservation/performance evidence on the signed
candidate. The current Python exposure selects fixed quotas only. No smaller
reservation, passing allocator test or software instruction count alone proves
speed. Native and complete-Python same-run controls, raw classifications and the
maintainer's release decision remain required by the existing protocols. This
step introduces no allocator service, delta API, private-provider claim or release.
