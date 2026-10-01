# Binding input transactions — 0.17.0

Python and the common JavaScript/TypeScript binding consume the published
`maelys/datalog_transactions.h` API. No engine source, consumer API version,
backend ABI 5/6/7, window contract, allocator service or solver algorithm changes.
The native reference path may recompute all derived facts.

| Operation | Python | JavaScript / TypeScript |
| --- | --- | --- |
| Attach before first successful solve | `session.inputs(...)` | `session.inputs({...})` |
| Read committed identity | `inputs.base` | `inputs.base` |
| Replace dynamic input and solve | `inputs.replace(base, facts)` | `inputs.replace(base, facts)` |
| Apply a supplied delta and solve | `inputs.apply(base, added=..., removed=...)` | `inputs.apply(base, {added, removed})` |
| Detach (no live result) | `inputs.close()` | `inputs.close()` |

Facts retain each binding's existing representation and exact int64 conversion.
Bases carry unsigned 64-bit incarnation/generation, represented by Python `int`
or JS `bigint`, with no floating-point round trip. They are explicit optimistic
concurrency tokens, not authentication credentials. A successful no-op advances
the base; failures preserve it. Additions win; duplicate and absent-removal
semantics, frozen vocabulary, raw bounds and diagnostics are inherited from C.
Two valid attachment configurations may have equal fingerprints but different
incarnations. Closing does not undo a successful transaction or reset session
history. Bindings never reinterpret an expired token or hide a retry.

All user iterators/getters are evaluated under a binding reentrancy guard before
native publication. The native transport decodes both batches before calling the
C transaction function. A malformed second batch must not publish the first.
Each operation returns the ordinary result type, including query access checks,
fingerprints, typed enumeration and explanations. Parent close releases results
before attachment storage; explicit attachment close refuses a live result.
Python retains its thread confinement, and native Node its per-profile mutex.

## Storage contract

The session reserves its existing fixed native workspace at creation. Python
allocates and aligns caller-owned attachment bytes using the public sizing call.
The JS C adapter allocates a handle, attachment arena and bounded conversion
buffer, plus a temporary vocabulary pointer array when needed. The conversion
buffer holds `max(factCapacity, additionCapacity + removalCapacity)` raw facts.
The attachment owns two canonical banks and add/remove scratch. Capacity and
overflow checks precede allocation. A partially failed attachment releases every
owned block and leaves the session attachable. There is no per-fact native heap
allocation, growth, or solve-time fallback.

Native allocation tests cover base read, replace/apply, query, result release
and rejected-batch reuse with allocators disabled. They do **not** claim zero
allocation for Python, JS, transport encoding, compilation or explanation text.
Bindings allocate while staging values. The complete Python lifecycle remains
subject to the separate performance report and maintainer decision.

## Qualification

- Independently installed SDKs, SMALL and LARGE, for Python and native/WASM.
- Differential snapshot oracle for 80 generated transactions per binding/profile
  (seed 170017, Python PRNG and JS uint32 LCG specified in the tests); intermediate
  decisions compared after additions, removals and explicit replacements.
- Native/WASM/Python independent trace: exact int64, typed decisions, generation,
  canonical explanations, fingerprints and native rejection diagnostics.
- Stale/foreign bases, attachment replacement before solve, fixed vocabulary,
  raw duplicate bounds, zero bounds, int64/uint64 boundaries and failed reuse.
- Solver rejection including numeric domain/overflow, matching ordinary-solve
  diagnostics; live-result leases and cascading close with retained input.
- Reentrant and unbounded iterators/getters, malformed wire lengths/offsets/kinds,
  partial allocation failures, all-engine allocation guard, ASan and UBSan.
- Real npm archive consumption in CJS/ESM and TypeScript NodeNext/bundler modes;
  browser and module worker use the same transaction API. Existing four-target
  packaging, OS/export checks and installed SDK WASM tests remain required.

The wire revision changes privately from 1 to 2 to reject mismatched JS and C
transport artifacts. It is not a new public C ABI. Generated results and receipts
belong in run artifacts, not Git. No timing claim follows from passing these
semantic/allocation checks; 0.16.0's accepted Python reports do not approve 0.17.0.
