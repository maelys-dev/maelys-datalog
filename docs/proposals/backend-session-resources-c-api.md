# Fixed session capacities — proposed C declarations (step 1)

Status: declaration review, 2026-09-28, after the accepted amendments in
[PR #141](https://github.com/maelys-dev/maelys-datalog/pull/141). This is step 1 of
[the 0.14.0 directive](backend-session-resources.md), before execution code.
The companion [proposal header](backend-session-resources-v6.h) is compilable
review material under `docs/`; it is not installed or included by any build.
Names, values and record boundaries here are concrete proposals for review.
They become an SDK contract only with a separately reviewed implementation.

0.14.0 is FIXED-only. No allocator types, callbacks, setter, cap, service pointer
or elastic execution path are declared. No delta entry is declared here. The
reserved elastic mode and allocator bit are rejected even by a provider that
claims to support them. Their implementation/qualification remains in the
[deferred allocation proposal](backend-allocation-015.md); its historical 0.15.0
filename does not name a delivered feature or a current release commitment.

## 1. Declaration inventory and existing interfaces

| Proposed declaration | Purpose |
| --- | --- |
| `session_resource_request_t` / `session_config_set_resources` | Copy a presence-masked E/D/S/T request into the existing opaque config; explicit zero differs from omission. |
| `session_resources_t` / `session_get_resources` | Return normalized effective values separately from program/build bounds. |
| `backend_v6_t` / `backend_reference_v6` / `session_config_set_backend_v6` | Typed ABI 6 selection; requirements and prepare receive the same effective resources. |
| `session_storage_plan_t` / `session_storage_requirements_configured` | Checked read-only sizing, with internal/external component accounting. |
| `session_init_configured` | Initialize the existing session handle in caller-owned storage using the policy/configuration plan. |
| `extension_v2_t` / `context_register_v2` | Atomically register typed ABI 5 and/or ABI 6 provider arrays with existing frontend/filter/planner components. |
| `session_config_set_context_backend_v6` / `context_backend_v6_count` / `context_backend_v6_info` | Explicit ABI 6 context selection and enumeration without changing the legacy registry record or selectors. |

All names in the table have the existing `maelys_datalog_` prefix. No new session
handle, global mutable backend selector or loader is introduced. Ordinary
consumers continue with opaque config/session handles; descriptor types remain
advanced extension-author interfaces. At implementation, place consumer records
and functions in the consumer header, and V6/extension records in appropriate
advanced headers without adding a dependency on a private provider.

Keep `MAELYS_DATALOG_BACKEND_ABI_VERSION == 5`, `maelys_datalog_backend_t`,
`session_options_t`, `session_create_ex` and all ABI 5 selectors unchanged.
The new identifier is `MAELYS_DATALOG_BACKEND_V6_ABI_VERSION == 6` and its type
is distinct. The V6 descriptor is not an ABI 5 record with changed callback
arguments. Do not cast between them or reinterpret a legacy descriptor prefix.

The configuration stores a discriminated selection. Legacy and V6 setters
replace that selection explicitly, preserving resource requests and separately
configured arenas; context and direct selections remain mutually exclusive.
An explicit ABI 5 descriptor, even one copied from the reference, stays ABI 5.
For a configuration without explicit provider selection, the canonical built-in
reference supplies the V6 fixed planning path when required. Existing default
constructors preserve their behavior and identity; no third-party provider is
silently migrated to gain sized storage.

`session_create_configured` retains its existing signature and gains the
admitted fixed request through its opaque config. For ABI 5 defaults it keeps
legacy construction. New whole-session planning/init accepts only the built-in
sized reference or an explicitly selected V6 provider; a non-default ABI 5
request is refused before prepare on every construction path.

## 2. Feature values, negotiation and quotas

The resource-feature namespace is separate from `MAELYS_DATALOG_CAP_*`:

| Proposed resource bit | Meaning in 0.14.0 |
| --- | --- |
| `SESSION_CAPACITIES = 1 << 0` | Supported. V6 descriptors must advertise it; admitted values still depend on program/profile/provider. |
| `CALLER_ALLOCATOR = 1 << 1` | Reserved, not included in the 0.14.0 supported mask. Any required request or provider advertisement is `UNSUPPORTED`. |

Memory modes occupy uint32_t fields: `FIXED = 0`, `BACKEND_ELASTIC = 1` reserved
and refused. All other modes and required resource bits are unsupported. A mode
is checked independently of feature bits: elastic with a zero mask still fails.
Resource admission must not change language/work capabilities or `program_info`.

Capacity presence bits are E=1, D=2, S=4, T=8. Unknown presence bits are malformed
(`INVALID_ARGUMENT`). An absent field uses the queried build default; a present
zero is exact. Inactive numeric fields are ignored and normalized away, not
hashed. The setter performs size/version/mode/mask validation and copies the
request without allocating a new config or mutating the old one on failure.
It cannot validate rooted program vocabulary before a policy has been selected.

Planning/creation normalizes once per operation and validates ceilings and rooted
vocabulary. Public requirements queries are independent planning operations;
construction recomputes rather than trusting an earlier mutable request/plan.
A representable request above a loaded ceiling or incompatible with rooted
program vocabulary/provider bounds is `UNSUPPORTED`, with the failed dimension
in diagnostics. Runtime exhaustion of an admitted quota is `PAYLOAD_TOO_LARGE`;
malformed records and arithmetic overflow remain `INVALID_ARGUMENT`.
Within construction, requirements and prepare receive identical normalized
E/D/S/T, mode and required resource features. The record is borrowed for each
callback; providers copy retained scalar values, not descriptor pointers.

When any effective quota differs from its default, normalization adds the
SESSION_CAPACITIES requirement. Explicitly equal quotas without an explicit
resource requirement normalize to the legacy default contract. A caller may
also explicitly require SESSION_CAPACITIES at defaults; that additional backend
requirement is honored, so ABI 5 rejects it even though the vector is unchanged.
No required feature is dropped to make a provider admissible. In ABI 5 default
admission the effective required resource mask must therefore be zero.

A scalar query does not turn a capacity into a promise that every input succeeds.
Raw E, complete D and rooted S/T meanings remain those of the directive. There
is no new per-predicate quota, capacity profile enum, or literal LARGE default.

## 3. Stable boundaries and readers

Every new resource input begins with, in order:

```text
size_t struct_size
uint32_t contract_version
uint32_t memory_mode
uint64_t required_features
```

This prefix is checked before reading any optional tail. It is a native C ABI
record, not serialized bytes. Use the proposal's offsetof/sizeof boundary
expressions for the target ABI; do not hard-code LP64 offsets. Preserve field
offsets, alignment and prefix meaning when extending it. A caller supplies a
properly aligned object with at least readable `struct_size` storage and the
promised accessible extent; arbitrary invalid pointers are not sandboxed.

The request accepts these boundaries:

- Prefix end: omitted mask and quotas mean all defaults.
- End of `capacity_mask`, then end of E, D, S or T: fields through that boundary
  are readable; a presence bit naming an absent field is invalid.
- Full V1 size, or a larger accessible object: V1 fields are readable and the
  unknown optional tail is ignored, unless a required feature rejects it first.

A size below the mandatory prefix, inside a known input member or at an
unpublished gap is invalid. Boundary expressions include whole members; native
padding is never a value to compare or hash. Known reserved fields must be zero.
Unknown version or required feature returns `UNSUPPORTED` before provider calls.
Do not read optional fields by first copying `sizeof(local_record)` bytes.

Effective-resource outputs require the whole V1 payload. The plan uses its own
`plan_version` and a stable size/version/reserved/required-feature prefix. For
these output records, validate the prefix and requested features first; a shorter
than V1 output returns `STORAGE_TOO_SMALL` without changing its payload. A larger
output receives only known members; preserve caller `struct_size` and unknown
tail bytes. Do not clear an entire unknown future object. Getter failures leave
outputs unchanged; constructors clear `*out_session` before other failure work.
An implementation must stage all fallible output preparation before writing it.

Keep the named V1 types and their `sizeof` boundaries unchanged after publication.
A later extension uses a distinct larger type containing that V1 record at offset
zero, sets its prefix size to the full object, and declares a required feature
for every new semantic requirement. Do not redefine V1 size as the latest size,
insert a field before its old end, or use old trailing padding for new members.
This also avoids embedding an evolving record by value in a frozen output plan.

For a V6 provider, require the entire defined descriptor through `destroy`;
validate version, identities, language capabilities, mandatory callbacks and
resource features before copying known members. Larger compatible optional tails
are ignored; newly required semantics must be negotiated through the resource
record. V1 extension registration stays exact-size; proposed extension_v2
registration requires its complete typed V2 descriptor. Its provider arrays have
the declared base-type stride and exact element sizes; do not cast an array of
larger descriptor extensions to them. Optional standalone descriptor tails do
not change array stride. Neither changes the
existing ABI 5 or extension ABI 1 reader.

A future allocation service can append resource fields behind this prefix
without changing either V6 callback signature. The sizing callback remains
read-only and cannot call that service. Exact allocation extension declarations
and its charge function are deliberately deferred; this review freezes only the
additive route and old-reader refusal behavior.

## 4. Requirements, arenas and context registration

Requirements and initialization take policy, policy index and the same opaque
config. The new requirements call returns the plan without preparing a provider
or allocating execution storage. Zero backend bytes is valid, but alignment
must remain a supported nonzero power of two. Overflow is rejected before use.
Init recomputes/validates its plan and fails without publication if storage is
short, misaligned or overlaps known live ranges. Config/policy mutations between
query and init cannot authorize an obsolete plan.

The plan's arithmetic is:

```text
arena_bytes = host_bytes + backend_bytes + explanation_bytes + padding_bytes
total_execution_bytes = arena_bytes + external_backend_bytes + external_explanation_bytes
```

Internal component bytes are disjoint; padding includes inter-slice alignment.
Host bytes include session metadata, inputs, dictionary, required export/result
storage and any private policy copy. Independently supplied backend/explanation
buffers contribute their assigned required slices to external components and
zero to the corresponding internal component. All components are counted once;
extra unused caller buffer capacity is outside the plan. A NULL/zero backend
requirement does not reserve a hidden pool. Shared policy allocations, input
collectors, window adapters, stack, config and binding objects are reported
separately; this is not total application memory or RSS.

Caller init does not free supplied storage and performs no session allocation.
The convenience path uses the same plan with a documented allocation count;
the fixed reference target remains one session arena. Partial preparation invokes
the provider's existing destroy callback on any returned state, including NULL,
and publishes no session. Subsequent session close retains the current leases,
policy ownership and external-storage rules.

Extension V2 copies all typed components/identities atomically before sealing.
It does not execute backend callbacks at registration. Existing duplicate-name,
identity, catalogue-capacity and lifetime rules remain; one external backend name
cannot ambiguously select both ABIs in one context. Built-in reference selectors
provide their explicitly typed views. Legacy backend metadata enumeration stays
ABI 5; the two new metadata functions enumerate V6 entries. No legacy name lookup
falls through to a V6 callback. Looking up an existing backend of the wrong ABI
is `UNSUPPORTED`; an unknown name remains `NOT_FOUND`.

No new direct `session_create_ex_v6` shortcut is needed: the existing opaque
config composes resources, work and explanation settings. Default and new
construction paths still use the same session handle and close/query functions.

## 5. Execution identity encoding and examples

Let L be the existing 64-character lowercase hexadecimal execution fingerprint
for the same program, backend name/semantic ID, required language/work
capabilities, resolved work limit and compiled profile. The legacy algorithm
and its goldens remain unchanged.

If the normalized resources equal profile defaults, mode is FIXED and required
resource features are zero, return L exactly. Explicit equal capacities with no
additional feature requirement take this branch too. The descriptor ABI number,
request presence mask, record size and allocator reservation bit do not enter it.

Otherwise hash the following ASCII byte sequence using SHA-256 and return its
64 lowercase hexadecimal digits:

```text
maelys-execution-v2\n
L\n
E\n
D\n
S\n
T\n
memory_mode\n
required_resource_features\n
```

Each `\n` denotes exactly one LF byte, including the final one; display line
breaks in the block add no other bytes. Numeric fields use unsigned decimal
without leading zeros (except zero itself), signs, spaces, locale formatting or
NUL. L is ASCII hex, not 32 binary digest bytes. Mode is zero in this delivery;
non-default quotas add required bit 1. Unsupported modes/features are rejected
before computing an execution identity. Addresses, occupancy and native record
padding are never serialized.

These synthetic encoding vectors fix framing independently of a compiled policy;
they are not runtime evidence. Use L = 64 ASCII zeroes:

| E | D | S | T | Mode | Required resources | SHA-256 of the 103-byte V2 sequence |
| --- | --- | --- | --- | --- | --- | --- |
| 16 | 32 | 32 | 2048 | 0 | 1 | `1f0259dc1ecc6f26426c55dccea0f39f514edd9f7e98736c4f2f09f058b1e843` |
| 16 | 33 | 32 | 2048 | 0 | 1 | `214f5713074596400802764b6eb3a282d0e86f73d8874f1380abd97f2b81fafc` |

Changing only request presence/ignored values while preserving this normalized
record leaves the fingerprint equal. Changing D changes it as shown. Default
identity passthrough is tested against real existing goldens, not the synthetic
zero digest. A required allocator bit or elastic mode yields UNSUPPORTED, not
another vector. Window equality compares the resulting identities as before.
Future profiles contribute their own profile identity through L; no new public
profile enum or closed SMALL/LARGE list is required by this encoding.

## 6. Review and implementation tests

Declaration validation checks C11/C++17 syntax, coexistence with all current
headers, initializer arity and callback types. This does not validate dispatch,
bounds, rollback, storage budgets or any new engine behavior.

Implementation must exercise the directive's reduced section 9 matrix, including
these exact record rules with separately compiled callers/providers. Include
unknown and reserved required bits, elastic without its bit, prefix-only defaults,
a presence bit beyond available bytes, short outputs and untouched unknown tails.
The `program_info` quota-smuggling negative control remains required.

For future XLARGE, all four new quotas and all storage byte counts are size_t,
with no public arrays or masks sized from LARGE. Existing structural language/IR
constants remain separately documented. The required public-boundary fixture
supplies synthetic ceilings above LARGE and round-trips values through separately
compiled consumer/provider records/accessors; profile layout checks compare
public sizeof/alignment/member offsets. Keep current-profile over-limit rejection
as a separate engine test. Neither this header's syntax nor that mock certifies
an XLARGE engine or raises its current internal index limits.

Review these declarations and encoding before moving them into installed headers
or writing execution code. The allocation service matrix remains in that deferred proposal;
no extra growth qualification is introduced through this step-1 proposal.
