# Fixed session capacities — accepted 0.14.0 directive

Status: accepted scope with three maintainer amendments, 2026-09-28. Baseline:
0.13.0, `41372dc2a5ba329f4088ac4ab1780e7b02a982b7` on `main`. Acceptance authorizes
this directive and proposed C declarations before execution code; it does not
certify an implementation or authorize a release. Backend ABI 6 is the delivery
vehicle proposed here, not an ABI already installed by this documentation change.

Option B is selected: E/D/S/T session quotas are normalized once and passed
identically to backend storage requirements and preparation. Program/build
limits keep their meaning. ABI 5 stays intact and separately dispatched.
0.14.0 supports **FIXED only**; BACKEND_ELASTIC and the reserved allocator feature
are refused everywhere. The allocation service is planned as an additive
[0.15.0 delivery](backend-allocation-015.md), not an optional 0.14.0 deliverable.

This extends the [sized-reference proposal](../architecture/sized-reference-sessions.md)
to explicitly admitted external backends and narrows the broader
[resource design](../architecture/session-resource-contract.md) to this delivery.
Step 1 is now available as [proposed C declarations and identity vectors](backend-session-resources-c-api.md),
with a non-installed proposal header. Declaration review still precedes runtime code.

It does not change the current [ABI 5 contract](../architecture/compiler-backends.md),
import private implementation code, or implement the [future delta protocol](backend-transaction-deltas.md).

## 1. Delivery boundary

0.14.0 delivers effective E/D/S/T quotas, checked host/backend planning, fixed
execution, caller-owned session storage and explicit backend admission. Defaults
remain available. Smaller quotas must produce measured capacity-sized storage
where a saving is claimed; smaller admission numbers on unchanged arrays are
only quota enforcement. Both host and backend reserve execution storage before
use. No growth, allocation service, finite elastic cap or allocator callbacks
belong to this delivery.

The extension prefix, reserved BACKEND_ELASTIC value and reserved allocator bit
are retained with refusal tests. A reservation is not a supported capability:
0.14.0 rejects a request or provider advertisement of that bit before prepare,
and rejects BACKEND_ELASTIC even if the bit is absent. No private provider can
opt into an unfinished service by setting it. Record evolution tests establish
that the later service can be added without changing the ABI 6 callbacks.

Sizing is **downward within SMALL/LARGE ceilings only**. XLARGE is the planned
future third compilation profile, added alongside SMALL and LARGE. It is not a
0.14.0 quota value, runtime growth mode or published byte/capacity promise. Its
addition must preserve public API/ABI shapes and existing profiles; its internal
representation, actual ceilings and qualification remain separate work. Section
9 requires a test that public APIs do not freeze LARGE's capacity constants.
Language/IR structural constants are distinct from E/D/S/T storage capacities;
this is not permission to raise public arity or IR bounds silently.

Host elasticity, per-predicate quota changes, configurable provenance limits,
profile catalogues, strict no-heap artifact certification and production elastic
backend qualification remain outside 0.14.0. The allocator design and its failure
campaign are retained exclusively in the 0.15.0 document.

## 2. Delta decision and evidence boundary

The maintainer's direction for the future delta API is **L**, linear composition,
with a distinct snapshot entry for complete replacements. T stays in the
experimental bench so its negative result remains reproducible. Neither a
runtime L/T selector nor an automatically chosen crossover threshold is implied.
The snapshot entry must not manufacture N removals and N additions merely to
route a full replacement through the delta entry.

The written evidence reviewed here is [PR #140](https://github.com/maelys-dev/maelys-datalog/pull/140)
at `5b29e4850326af85d3c638ba9dacd158b8ed7a93`, report SHA-256
`92c24e2ea4cae34bd86dafae6c24533f2c4d261302391a34b8843d35ea8f70d7`.
On 2026-09-28 the PR was open and its 17 checks passed on that revision;
[the CI run](https://github.com/maelys-dev/maelys-datalog/actions/runs/36450794455)
is not a hosted performance campaign. Merge remains David's decision. These
document changes neither merge the PR nor depend on its unmerged code.

Preserve the exact interpretation of the local Linux ARM64 Docker/Callgrind
campaign, SMALL/LARGE, four-path order A1/B1/L1/T1/T2/L2/B2/A2:

| LARGE engine steady case | Observation in host software instructions (Ir) |
| --- | --- |
| Projection, 256 symbols, one removal plus one addition | T versus L: **-0.60%**, a small reproducible instruction difference, not an effect dismissed as rounding. |
| Projection, 256 symbols, full replacement | T costs **+10.78% relative to L**; L saves **9.73% relative to T**. The denominator changes. |
| Inert, 256 integers, full replacement | L versus A: **+24.89%**, the net change over the complete measured host path. Validation of 2N raw delta entries contributes; composition, copies and displaced work all remain in the exclusive decomposition. |

The last percentage is not a measurement of validation alone or of host plus
provider instructions. These are software counts, not latency, cycles or a
universal crossover rule. The unsuccessful T criterion and all exceptions remain
in the original report. No remeasurement or rewrite of historical evidence is
required to carry this decision into the capacity directive.

Capacity semantics must accommodate either future input entry without changing
program limits. Snapshot raw-count capacity and future raw add/remove batch
capacities are distinct dimensions; E must not silently become their sum or a
bound only on deduplicated deltas. Their exact delta encoding remains in that
separate design. ABI 6 capacities alone confer no delta capability: the existing
complete canonical snapshot, complete IDB publication and commit/abort protocol
continue to apply.

## 3. Program bounds, session quotas and occupancy

`program_info.max_input_facts`, `max_derived_facts`,
`max_facts_per_predicate`, representation widths, language limits and `limit_get`
remain program/build bounds. Do not pass smaller values through a modified
program view to make an ABI 5 provider appear capacity-aware.

The new session request has a presence mask for E/D/S/T; omitted dimensions use
the current profile default. A present zero is a real zero, never a default.
Unknown presence bits are rejected. Normalization happens once before planning,
uses checked arithmetic and produces immutable effective values:

| Dimension | Effective meaning and admission |
| --- | --- |
| E, input facts | Maximum raw facts supplied to a snapshot before deduplication, and capacity of its materialized input pool. Compiled policy facts are not charged as dynamic inputs. |
| D, derived facts | Complete derived IDB, including non-query helpers, independently of E. Existing per-predicate and policy-fact accounting remains unchanged. |
| S, symbol entries | Complete transaction dictionary, including program-rooted vocabulary. Must fit the already compiled program dictionary. |
| T, symbol text bytes | Interned dictionary text including NUL terminators, with program text included. Repeated strings consume shared text, not worst-case bytes per occurrence. |

Each value must fit its existing profile ceiling. Do not silently clamp, widen
indices, increase bounds, or grow quotas at runtime. Zero E/D is admissible when
the program/backend combination supports it; later input/derivation fails at
that bound. Zero S/T is admissible only when the rooted dictionary fits it.
Admission does not promise that every evaluation fits D, intermediate state,
per-predicate or work bounds. Exhaustion never means a successful partial result.

Preserve the complete dictionary hash index initially. Sort/insert scratch is
sized from raw fact/term occurrences where required, not merely distinct S.
Whole-batch validation precedes publication of facts or text. Rejecting input
preserves the existing byte-exact dictionary/preflight rollback contract.

Expose effective quotas separately from build limits, live occupancy and reserved
bytes. Identical effective requests have identical semantics regardless of how
defaults were expressed. An explicit vector equal to defaults is normalized to
the default resource contract.

## 4. Backend admission before preparation

Language capabilities, supported resource features, caller requirements and
selected memory mode are separate concepts. Resource support is explicit, not
inferred from backend names, copied function pointers or descriptor identity.
ABI 6 providers advertise fixed session-capacity support and may reject specific
program/capacity combinations in read-only requirements. The reference supports
fixed capacities. No provider advertises the reserved allocator feature in 0.14.0.

| Selected path | 0.14.0 admission |
| --- | --- |
| Legacy constructor/configuration, reference or ABI 5 provider, defaults | Preserve behavior, storage requirements and callbacks. |
| ABI 5 provider, explicit E/D/S/T equal to defaults, no additional required resource feature, legacy host construction | Normalize to defaults; no quota record is sent to the provider. |
| ABI 5 provider, any non-default effective quota or explicit session-capacity resource requirement | `UNSUPPORTED` before `prepare`; no alternate backend. |
| ABI 5 provider, new caller-owned whole-session constructor | `UNSUPPORTED`, even at defaults. Existing backend-arena provisioning remains supported. |
| ABI 6 reference or fixed provider, supported quotas and FIXED | Compute/validate the actual host/backend plan, then prepare. |
| Any request selecting BACKEND_ELASTIC | `UNSUPPORTED` before requirements/prepare, even without a feature bit. |
| Any request requiring, or descriptor advertising, the reserved allocator bit | `UNSUPPORTED` before callbacks. Being known/reserved does not make the bit supported. |
| Unknown required resource feature or mode | `UNSUPPORTED` before callbacks; never silently ignored. |

Omission of memory mode means FIXED. There is no allocator setter in 0.14.0.
Future tails cannot smuggle an allocator through this host: their required bit
is refused even if their bytes are ignored. Backend/ABI/mode incompatibilities
are rejected without invoking provider callbacks. Program-dependent supported
bounds may be checked by read-only requirements. Failure publishes no session.

## 5. ABI and configuration extensibility

ABI 5 remains an exact, separately dispatched contract: preserve its record
layout, signatures and default behavior. Introduce ABI 6 descriptors and explicit
selection/registration entry points rather than changing the calling convention
behind an ABI 5 function pointer or casting one descriptor to the other. Keep
legacy source declarations available; select the new descriptor through a
distinct versioned type. Update context/module registration and installed SDK
examples accordingly. No opaque-pointer reinterpretation is a migration plan.

The ABI 6 resource record has a stable mandatory prefix containing `struct_size`,
a resource-contract version and a required-feature mask. Version 1 defines the
capacity presence/effective-value rules and FIXED mode. The elastic mode and
allocator feature are reserved but unsupported; no cap/service fields are
needed in the 0.14.0 prefix.

The consumer-facing request is normalized into a separate read-only record for
the backend. Retain explicit initializers; C padding is not configuration.

Extensibility rules are part of the implementation, not a consequence of the
field's name:

- Reject a record shorter than the mandatory prefix. Read a known field only
  when its complete published extent lies within `struct_size`; reject a record
  truncated inside a known input field. Short outputs fail before payload writes;
  published compatible prefixes are tested.
- Missing optional fields take their documented defaults. Never read the local
  `sizeof` from a shorter caller object, or overwrite a short output record.
- Reject unknown contract versions, required features and selected modes with
  `UNSUPPORTED`. Reject malformed presence/size/reserved-field values with
  `INVALID_ARGUMENT`.
- A larger record may contain an ignored optional tail. Every future non-default
  semantic requirement must carry a feature bit in the stable prefix. An older
  host/provider rejects that bit instead of silently ignoring the requirement.
  Extensions that change existing semantics require a new contract version/ABI.
- Copy understood fields into host-owned immutable configuration. Do not retain
  caller descriptor pointers or hash padding, absent fields or unknown tails.

Apply these rules to every newly extensible record, including output-plan
descriptors; future allocator descriptors follow these rules when introduced.
Do not retroactively relax ABI 5's size checks. Test old/new caller-host-provider combinations using separately compiled consumers;
equal source definitions in one test binary do not demonstrate compatibility.
This mechanism leaves room for later extensions without freezing an unqualified
allocator now. It is not a promise that every future ABI change is compatible.

## 6. Requirements, construction and identity

The callback shapes below describe information flow, not installed declarations:

```text
storage_requirements(program, effective_resources) -> arena bytes/alignment
prepare(program, effective_resources, arena) -> state
```

Both callbacks receive identical normalized capacities and memory policy.
Requirements is read-only, deterministic for those inputs and backend identity,
and cannot allocate or reenter the engine. It reports the backend arena,
which must contain all backend execution storage in this fixed-only delivery.
Backend algorithms choose their private layout and may reject unsupported
capacities, but cannot secretly replace the accepted quotas.

The host combines its own requirements with backend storage, alignment gaps,
retained private policy copies, session metadata and selected explanation
workspaces. Canonical export is reserved only where the solve path needs it.
Every addition/multiplication/alignment operation is checked. Report each
component and the aggregate so separately supplied storage is counted once.

The caller-owned constructor validates the actual plan, short/misaligned buffers
and overlaps among known live ranges before publication. Unknown external arenas
remain the caller's disjointness obligation. Buffers stay aligned, exclusive and
immovable through session destruction; closing never frees caller storage.
Partial initialization may alter scratch/arena bytes, but releases acquired
resources without further acquisition and leaves no published handle.

The allocating convenience constructor uses the same plan and publishes its
allocation count separately. The reference target is one session-arena
allocation; prove it rather than treating the proposal's projections as measured
bytes. Configuration/policy/binding allocations remain separately accounted.
Both ownership paths preserve existing policy lifetimes and result leases.

A plan is not valid across changed program/backend identity, profile, quotas,
memory policy or explanation provisioning. Recompute or verify these inputs at
initialization. Publish target-specific static bounds/alignment separately when
claiming build-time provisioning; a runtime query is not a static upper bound.

Preserve the current execution fingerprint for the normalized default fixed
path, including an ABI 6 adapter with unchanged semantic identity. Non-default
contracts use a versioned canonical encoding that binds effective capacities,
FIXED mode, backend semantic policy, required execution features and work
limits. The deferred allocator delivery defines its additional policy identity.
Do not hash addresses, occupancy, current reservation, record padding or the
descriptor ABI number.
Before implementation is accepted, publish exact encoding vectors and window
compatibility tests; capability bits alone are not an execution identity.

## 7. Reserved extension and deferred additive delivery

Reserve the BACKEND_ELASTIC mode and allocator feature identity without an
implemented path or supported-capability claim. Both are refused in 0.14.0.
The [revised allocation proposal](backend-allocation-015.md) retains the
service, cap accounting, phases, acquisition failure replay, native fixture and
negative controls. Its historical filename does not imply shipped allocator
support: v0.15.0 delivered the common JavaScript binding. None is a condition of
the fixed-capacity delivery.

The service will extend versioned resource records behind the unchanged prefix
and required-feature mask. It must not require another ABI 6 callback parameter,
change an existing field's meaning or silently activate for a fixed session.
0.14.0 readers reject its required bit before looking at an unknown tail. If the
later design cannot meet those additive constraints, revise that design before
claiming compatibility; the reserved bit alone does not prove it.

## 8. Fixed transactions, errors and windows

Preserve existing commit/abort, result and explanation leases. Backend committed
state stays intact until host acceptance; every journal and undo obligation fits
the fixed plan. Publication and commit are infallible and allocate nothing.
Rejected input preserves existing byte-exact dictionary/preflight guarantees;
candidate scratch is not committed state. Existing ignored-output-error and
work-budget behavior remains authoritative.

| Failure | Status and diagnostic |
| --- | --- |
| Malformed configuration, invalid alignment or checked size overflow | `INVALID_ARGUMENT`, before mutation/provider call |
| Unsupported backend/ABI/resource combination, including reserved elastic mode/bit | `UNSUPPORTED`, before prepare |
| Short supplied arena | `STORAGE_TOO_SMALL`, with required size/alignment |
| Exceeded admitted E/D/S/T | `PAYLOAD_TOO_LARGE`, with effective dimension/bound; no partial result |
| Violated result/explanation lease | `INVALID_STATE`, preserving the lease and prior committed state |

No new status or build-limit selector is introduced for these quotas. Diagnostic
fields distinguish an effective session bound from a program/build limit. Existing
callback validation, native mappings and CFFI/Python behavior remain coherent.

The two window sessions have equal effective execution identities and disjoint
fixed storage. Count adapter storage and both sessions separately. Preserve
static replacement, expiry, cursor/watermark rollback and readable explanations
under rejection, including a removal enabling more IDB through negation. A no-op
expiry advances only time: no backend call or result-lease change. Abandoned
initialization probes commit nothing. Retain these existing fixed-path lease
checks; allocator-specific late rejection is deferred to 0.15.0. The modeled
window in #140 does not substitute for the real adapters' tests.

## 9. Acceptance evidence blocking 0.14.0

This matrix covers the fixed-capacity delivery only. The allocator admission,
acquisition failure, late rejection after growth and allocator lifecycle rows
are in the [0.15.0 qualification matrix](backend-allocation-015.md#3-qualification-belonging-to-0150).
They do not block 0.14.0. Existing fixed rollback/lease guarantees remain required.
Run the delivered capacity combinations in SMALL/LARGE using clean installed
SDKs and static/shared linkage; record candidate/consumer revisions and commands.
Public fixtures do not certify an untested private provider.

| Area | Required 0.14.0 evidence |
| --- | --- |
| Defaults and ABI 5 | Unchanged program views, callbacks, requirements, canonical output, leases and fingerprints; separately compiled legacy consumer; non-default quotas and new whole-session arena rejected before prepare. |
| Capacity admission | Defaults, two smaller coexisting sessions, zero/one/full/overflow, rooted vocabulary, raw duplicates, D helpers and per-predicate bounds; no clamping or fallback. |
| Host/backend agreement | Recording ABI 6 provider sees identical normalized E/D/S/T and FIXED policy in requirements/prepare; failures publish no handle; copied/wrapped descriptors inherit no undeclared resource support. |
| Construction | Component bytes/alignment, short/misaligned/overlapping buffers, stale plan, arithmetic overflow, both policy ownership paths, failed fixed preparation cleanup and convenience-constructor allocation counts. |
| Fixed guarantees | Disabled engine allocators in input/solve/query/result release/configured explanations; full-capacity export, collision/wrap, canonical IDs, dictionary rollback, release reset bytes, existing window leases and reuse. |
| Record evolution and reservations | Separate old/new caller-host-provider builds; short prefixes/outputs, truncated fields, optional tails, unknown required bits/version/mode; reserved allocator bit and BACKEND_ELASTIC refused before callbacks. |
| Future XLARGE compatibility | Audit public declarations/layouts for LARGE-specific storage constants; a test varies the supplied E/D/S/T ceilings above LARGE and confirms the same public records, counts, accessors and capacity negotiation carry them without truncation or a closed SMALL/LARGE-only API. Current SMALL/LARGE engines still reject above-profile requests. |

The XLARGE test is a public-boundary compatibility fixture, not a shipping third
engine profile or a claim that wider internal representations work. Test public
record sizeof/alignment/member offsets independently of build profile; use
queried bounds and size_t/count-based contracts, not LARGE-sized public arrays
or hard-coded admission ceilings. Round-trip larger values across separately
compiled public consumer/provider fixtures with synthetic program/build bounds.
Audit language/IR constants separately: they cannot be relabeled as storage
quotas. A passing mock does not qualify a future XLARGE engine, but any public
LARGE-dependent shape or truncation blocks 0.14.0 until resolved.

Required negative controls are: a quota smuggled through `program_info`, ignored
resource requirements/reserved feature, elastic mode accepted, and publication
on a rejected fixed transaction. Preserve existing allocation/rollback tests;
no allocator service fixture or new growth campaign is required here. Sanitizer
and mutant results need a passing baseline; build/loader errors are not detections.

No generated measurements go into git. Hot-path changes retain same-compiler/
profile A/A and alternating A/B, checked sorted/reverse/duplicate/adversarial
inputs, canonical outputs, relevant instruction evidence and reservation/reset
budgets. Python changes retain the [complete lifecycle protocol](../python-performance.md)
in SMALL/LARGE; CFFI still allocates. Smaller reservations and #140 alone establish
no speed claim. This documentation change requires no new timing campaign.

## 10. Implementation sequence and completion record

1. Open the documentation PR for this amended directive. Then publish the proposed
   C declarations, exact extension-prefix boundaries and execution-identity
   encoding/vectors for review. No execution code precedes that review. A charge
   function and allocator declarations belong to 0.15.0, not this step.
2. Implement normalized fixed capacities and checked host storage, retain the
   reference defaults, wire ABI 6 requirements/preparation and explicit ABI 5
   admission. Bindings expose supported fixed capacities only.
3. Complete section 9, including the XLARGE public-boundary test, actual storage
   budgets and installed downstream replay. Record exact revisions, commands,
   outcomes and migrations. Existing human Python review remains in the release
   ceremony; successful CI is not approval of measured performance.
4. In 0.15.0, add and qualify the allocator service through the preserved extension
   mechanism. Its test provider and acquisition/lifecycle matrix travel with it.
5. Qualify a production elastic backend separately. Continue the L delta/snapshot
   work under its own remaining vocabulary, incarnation, bank and explanation
   obligations. T remains benchmark code for reproducing the negative result.

Scope acceptance is not implementation acceptance. These documents change no
installed header or runtime, and approve neither #140's merge nor a release.
