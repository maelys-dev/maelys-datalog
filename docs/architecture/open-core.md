# Open core and module boundary

The public core is a complete MPL-2.0 library. It includes the reference solver,
parser, negation, arithmetic, prepared sessions, diagnostics, explanations and
standard string filters. Separately compiled providers can add capabilities
without including or modifying private engine headers.

## Implemented boundaries

| Component | Responsibility | Interface |
| --- | --- | --- |
| Core | Parse, type/binding checks, snapshots, budgets, traversal, errors, provenance | `maelys/datalog.h` |
| Module registry | Validate/copy descriptors, assign internal IDs, seal lifetime | `maelys/datalog_module.h` |
| String provider | Validate a constant pattern, bound cost, evaluate bytes | `maelys_datalog_filter_module_t` |
| Planner provider | Choose the next candidate from a core-validated safe set | `maelys_datalog_planner_module_t` |
| Standard providers | Existing `starts_with`, `ends_with`, `contains` semantics | Same module SDK |

The current planner interface permits different heuristics. It is not a
replacement solver ABI: incrementality, vectorized execution and a full alternate
backend are separate future design work. No regex/refex provider is included.
The string boundary permits such a provider to be developed outside this repo.
The source language still has no string escape syntax; a provider must document
its supported pattern dialect instead of implying full Gitolite compatibility.

## Integration and lifetime

1. Build the provider using only installed `maelys/datalog_module.h` and its
   public dependency `datalog.h`. Do not include the legacy umbrella or `src/`.
2. Link it to the same engine instance as the host application, statically or
   dynamically. Do not embed a second copy of the engine inside the provider.
3. Register all descriptors on a single startup thread, before any policy is
   initialized or loaded. There are up to 16 external filters and one planner.
4. Load policies through the ordinary API, then create sessions and solve.

Registration checks ABI version, exact descriptor size, identifier syntax,
required callbacks, duplicate filter names/semantic IDs and capacity. Descriptors
and names are copied, so registration arguments may be stack-owned. Callback
code must remain loaded for the process lifetime. There is no unregister,
replacement, dynamic loader, per-policy module selection or implicit fallback.
The registry becomes read-only at the first ruleset initialization, including
initializations reached by legacy, Python and WASM loaders. A failed later load
does not unseal it. This avoids changing the meaning of live policy snapshots.
Standard-only initialization is synchronized, so concurrent first policy loads
do not require a new explicit initialization call. Registration itself is still
single-threaded startup work and must not race any policy loading or evaluation.

Callbacks are trusted native functions. They must be deterministic, bounded,
allocation-free, reentrant and free of I/O, locale/time dependencies, mutable
external state and engine reentry. The C boundary cannot sandbox a malicious
callback, enforce its execution time, or stop arbitrary memory writes. Register
only reviewed code. A future untrusted plugin system would need isolation.

The SDK is a versioned alpha contract, not a promise to accept every future
descriptor layout. Build with matching ABI headers and target architecture.
Python/JS do not expose language callbacks through this interface; a host must
register native providers at startup. WASM providers must be linked into the
same WASM module and registered before loading policies.

## Filters

The existing `filter(Value, "constant pattern")` form is reused. A registered
domain predicate of the same name retains the established precedence; avoid such
collisions when choosing module names. An unresolved name is rejected by loading.
Pattern validation runs before the parser commits the filter. Programs remain
bounded, pointer-free POD references to source-pattern bytes owned by the ruleset;
no module object/destructor is hidden inside a session snapshot.

The provider declares a conservative cost from the input lengths, with checked
overflow. External costs must be positive. The core charges its evaluation and
cost budgets before calling `evaluate`. Providers return a status and exactly
zero or one; malformed outputs and errors are fatal, not false matches. A native
provider must honor its own bound; callback execution is not forcibly metered.
The same dispatch is used by Why-false with its independent diagnostic budget.
Why-true records the provider name, semantic ID, value and original pattern.

The first ABI evaluates raw patterns. It has no compiled-program ownership or
cache API. A provider can use bounded stack working state; persistent compiled
artifacts would require an explicit extension to the snapshot contract.

## Planning and identity

The core retains semi-naive delta anchoring and variable-binding safety. It
offers only unplanned literals whose prerequisites are already satisfied. The
provider sees public metadata and chooses an array index. Out-of-range choices,
unwritten outputs and callback errors abort the solve. The core owns the index
map and every actual traversal and result. The default heuristic remains the
same when no provider is installed.

A different traversal can select another valid witness or hit resource bounds
in a different order; changing planners is therefore an execution-contract
change even if unbounded logical answers agree. Planner name and semantic ID
are included in the canonical policy identity. Filter IDs describe behavior and
cost accounting, not just a marketing version: bump the semantic ID when either
changes. IDs are assertions by the provider, not hashes or signatures of its
machine code. Use artifact provenance separately when binary attestation matters.

Manifest source SHA-256 checking still happens against the original bytes.
After parsing, extended policies receive a canonical executable identity. The
same identity flows into loaded policy sets, prepared sessions and explanations.
For standard policies, existing identity bytes are preserved. Policy loading
through the legacy low-level API still requires the caller to finalize a ruleset
with `maelys_datalog_ruleset_finalize_sha256` before consuming its identity.
Filter registration
order and unused filters do not affect canonical fingerprints. Raw internal
filter IDs and in-memory structs are not portable serialization formats.

## Builds and verification

`build-support/{core,native,standard}-sources.txt` are the shared source inventory
for CMake and all Make variants. Standard providers compile as a separate CMake
object target with only the public include directory. Third-party provider
sources are never discovered with a recursive glob or included in core releases.

```sh
make test
cmake -S . -B build/cmake
cmake --build build/cmake
ctest --test-dir build/cmake --output-on-failure
bash tools/check_module_boundaries.sh
bash tools/check_module_sdk.sh "$PWD/build/cmake"
```

The last check installs the library to an isolated prefix and compiles the
example provider and consumer with only that prefix's headers. The consumer
tests error propagation, startup sealing, prepared-session lifetime, explanation
output, recursive planning and fingerprints in independent processes. CTest also
links provider/consumer against both the static and shared engine.

## Licensing and product separation

Everything in this repository, including standard providers, SDK headers and the
example, is MPL-2.0 except explicitly identified third-party material such as
yyjson (MIT). Future independently authored proprietary implementations should
live in separate private repositories and be built into separate artifacts.
Using the SDK does not turn a private source file into an MPL file. Copying MPL
implementation code or modifying covered files carries different obligations;
directory names and linker flags do not change those licenses.

Consumers distributing the combined product must preserve notices and make the
covered MPL sources available as required by the license. Alternative licensing
of owned code or third-party contributions requires the corresponding rights.
This technical boundary does not itself supply a contribution assignment or
commercial license. See the [Mozilla FAQ](https://www.mozilla.org/en-US/MPL/2.0/FAQ/).
