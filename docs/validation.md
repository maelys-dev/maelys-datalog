# Validation matrix

Run suites sequentially. Separate SMALL/LARGE build directories prevent a stale
binary from silently exercising the other profile. Generated build artifacts are
not proof that tests ran; check the exit status and the suite summaries.

## Native C and sanitizers

```sh
make -j3
make -j3 test
make -j3 test BUILD_DIR=build/native-large CFLAGS='-Wall -Wextra -g -I. -Iinclude -DMAELYS_DATALOG_PROFILE_LARGE'
make -j4 -f Makefile.asan asan PROFILE=SMALL
make -j4 -f Makefile.asan asan PROFILE=LARGE
make bench-pipeline
```

Both Make test inventories use `tests/test_*.c`, including the policy-set
fingerprint suite. `asan` reruns every test even if its binary is up to date.
The executable inventory is derived from `tests/test_*.c`; report the actual
suite summaries rather than a hardcoded historical count.
ASan/UBSan run locally; macOS disables leak detection. The Linux CI enables
LSan. A local macOS PASS alone is not evidence of Linux leak safety.

The [multi-fact window](architecture/multi-fact-window.md) adds a separate FIFO/set
oracle and allocation guard, included in both native profiles and sanitizer
inventories. Tests distinguish groups, raw contributions and the unique union,
cover empty groups, shared facts, boolean normalization, last-contributor expiry,
full replacement, every storage bound, prepared leases, cursor exhaustion and
closed handles after session destruction. Fixed generated sequences use seeds
1, 0x752abc91 and 0xdeadbeef; mutations of a tuple, group ID and expiration must
fail the independent oracle. Backend failures before work and after emission
exercise retry, cleanup and reentry. Allocation hooks cover init through close
with already-created sessions; rejected attempts preserve every committed bank
byte and adapter header byte, excluding candidate scratch. The installed static
and shared SDK runs the functional suite and minimal multi-fact C example.

The [last-N window](architecture/last-n-window.md) tests cover atomic expiry and
recomputation against a separately maintained FIFO, all IDB and canonical IDs,
aggregate/negation/recursion behavior, capacity errors, injected backend failures,
prepared result leases and long vocabulary rotation. Its allocation guard checks
the complete engine during adapter initialization, pushes, query/explanations,
result replacement and destruction after session creation. Failed transactions
preserve the committed input bank and window metadata byte-for-byte; candidate
scratch is explicitly outside this comparison. The ordinary consumer also runs
outside the source tree against both installed SDK libraries.
The lifecycle regression closes a window, destroys its borrowed sessions and
calls every entry point on the closed handle while retaining its caller arena;
all reject with unchanged outputs/storage, including under ASan/UBSan. Tests
also cover same-arena reinitialization, allocation-free closed-handle rejection,
committed text usage (interning, exhaustion below N and expiry), and a release
failure unrelated to explanations with candidate cleanup and successful reuse.

Pipeline counters are compiled only under `MAELYS_TESTING`: the private header
`src/core/maelys_datalog_pipeline_testing.h` expands `MAELYS_DATALOG_COUNT_PIPELINE`
to nothing otherwise, so the tested translation units are the shipped ones and
the production libraries carry no counter code. `tools/check_module_sdk.sh`
verifies that outcome on the installed static and shared libraries with `nm`,
and `tools/check_module_boundaries.sh` keeps the instrumentation out of the
public headers. The Make pipeline test observes parse/validation/fingerprint/
preparation and materialization calls; CMake's static/shared pipeline tests
check the same 21 transcript goldens, two filter validations and four diagnostic
ordering checks. The benchmark's
CPU time is descriptive, not a throughput guarantee.

For the Why-false envelope migration, the pipeline asserts the exact new header
then projects only that header to the historical one before hashing. Unchanged
goldens therefore still detect any changed authority/program/execution
fingerprint or payload byte. `test_maelys_datalog_language_spec` separately
compares all three Why-false states byte-for-byte and sweeps every undersized
buffer. Its shared-envelope test compares both formatters' version lines and
checks their distinct `document=why-true` / `document=why-false` discriminators.
Python-next tests the discriminator for complete, truncated and
not-applicable output from actual solves, not constructed formatter fixtures.

Base-membership instrumentation is also restricted to `MAELYS_TESTING`.
Every structurally valid candidate head is compared with the historical linear
policy/EDB lookup, including candidates for which production skips membership.
Any disagreement aborts even under `NDEBUG`. The dedicated test checks zero
lookups on ordinary validated rules, both derivation traversals, stratification,
byte-identical facts/proofs/explanations and positive low-level controls in both
base sources. Those controls include an IDB fact manually placed in a base with
a stale `program_validated` bit; registry flags or that bit alone are insufficient.
At the per-predicate capacity, a base duplicate must still succeed; removing
that fact must expose the named overflow instead. Existing full-scan reference
entry points force membership as well as full EDB candidate scans.

The bounded-sort test compares canonical values with a libc reference for
ordered, reverse, equal, duplicate-heavy, organ-pipe, sawtooth and random input,
including overlapping ordered suffixes, profile capacity and an explicitly
exhausted introsort depth budget. The whole-engine allocation test includes
partition-sized input; performance does not replace its allocation assertions.

```sh
make -f Makefile.bench bench-all CC=clang
```

Compare the same compiler/profile on baseline and candidate, without concurrent
builds. Establish a per-case A/A noise floor before interpreting A/B ratios.
The solver benchmark includes EDB finalization, solve, query and result release.

The native materialization test compares indexed insertion with the historical
scan through the full EDB capacity, typed values and colliding/wrapping chains.
The 31/32/33 boundary tests cover duplicates, rejected activation, complete
backfill and storage reuse. Up to 32 distinct facts keep the scan; fact 33
activates the index. The last-fact shortcut runs only after activation, avoiding
an extra comparison on each distinct insertion in scan mode. This fixed policy
is not a measured universal crossover.
Capacity checks retain their ordering: a duplicate at a full predicate is still
rejected. Session failures restore symbols, facts, counts and scratch byte for
byte; the all-engine allocation guard also fills the per-predicate bound,
rejects a duplicate and reuses the session with allocation disabled.
The transient uint16_t fact index occupies 4 KiB SMALL / 8 KiB LARGE inside the
existing symbol-pointer scratch union. It adds no session memory or allocations;
finalization invalidates its offsets. Hash collisions still require full fact
equality and can degrade to a bounded linear probe; there is no worst-case
constant-time claim. The legacy direct EDB construction API retains its scan in a separate function
without an index parameter or branch; only validation is shared. Do not mix
indexed and unindexed insertions in one construction: the index cannot see
facts appended through the unindexed entry.

The symbol-table suite checks read-only lookup against known insertion IDs,
including a collision chain wrapping from the last bucket to the first, a full
32-bit hash collision, copied tables, embedded NULs, maximum-length values and
lookups after entry/text capacity rejection. Byte comparisons verify no table
mutation. In the indexed path, corrupt bucket references and an exhausted probe
cycle fail with `INVALID_STATE`; valid misses still return success with an invalid ID and
`found = 0`. The existing index adds no storage or allocations. Hashing costs
time proportional to the input length, so a table with at most `len` entries
keeps the scan; a larger table probes the index. This work-based policy is not
a measured universal crossover. The capacity test checks lookups as the table
grows through that boundary. Collisions can still require a bounded linear probe;
no universal constant-time or latency claim follows.

The manual benchmark's optional `diagnostic_original` input compares a baseline,
the original candidate and the revised head on native Linux x86_64. It reuses
the historical `solver_size_pure` fixture and payload in a separate driver;
`diagnostic_size` predeclares LARGE/1024 or LARGE/2048 (the default).
Callgrind collection surrounds only the payload after eight warmups:
finalization, solve, query and result release; preparation is excluded. Two
processes per revision/layout check repeatability. Ir counts executed software
instructions, not hardware retired instructions or elapsed cycles.

The runner records CPU models from `/proc/cpuinfo` and the kernel/architecture
before measurement. Every comparison and diagnostic report shows this identity
in its header, using the artifact's metadata rather than the rendering machine.
Older artifacts without it are labeled explicitly; consult their original logs.
A change of CPU prevents attributing a difference between runs to a code change;
matching CPU names alone do not establish identical conditions either. Within-run
comparisons retain their own A/A classifications and attribution limits.

The diagnostic links the same compiled objects with 0, 16, 64 and 256 unreachable
text bytes before the EDB object, checks the symbol displacement, and retains
all variants. Two A/A pairs per unpadded revision precede two interleaved rounds;
500 warmups and 1000 checked samples feed each timing pass. It preserves raw
samples, instruction profiles, function counts and disassembly as run artifacts.
This intentionally selected diagnostic does not replace the complete solver,
input and session matrices in the same sequential manual job. Its driver also
changes the binary layout relative to the full benchmark. Equal Ir alone does
not prove a layout cause; a timing change under a verified neutral perturbation
with unchanged instruction work demonstrates sensitivity for that case/run.
See the [Callgrind manual](https://valgrind.org/docs/manual/cl-manual.html) and
[Mytkowicz et al., ASPLOS 2009](https://sape.inf.usi.ch/publications/asplos09.html).
No generated measurement is committed and no diagnostic authorizes a merge.

Code placement and data layout need separate controls. Text padding does not
vary member offsets or object alignment. The diagnostic's layout snapshots
record native sizes/alignments, all existing ruleset offsets and actual fixture
addresses modulo 64 (an explicit diagnostic reference, not a portable cache-line
size guarantee). A divisible member offset does not establish absolute alignment.
Compare baseline/original/revised layouts in one run; changed field accesses can
also alter generated code. Restoring offsets or losing one above-floor timing gap
does not prove a universal zero-cost extension or establish a cache mechanism.

The input-allocation test checks colliding/wrapping hash chains and byte-for-byte
arena restoration on rejected batches, including empty strings in one/three-byte
text budgets. Performance acceptance is separate from these allocation gates.
The index uses a single uint16_t entry array: zero is empty, 1..40961 is a
committed offset plus one, and 40962 plus a batch ordinal is pending. A static
assertion bounds the combined ranges in both profiles. A uint16_t before-image
journal is bounded by `min(fact_capacity * 5, ceil(text_capacity / 2))`.
Reverse traversal of the validated batch prefix recovers each pending slot in
reverse insertion order, restoring even stale committed entries without changing
generations. Tests reject each field of mixed-value batches, cross offset 32768,
and exercise the highest pending ordinal at maximum fact capacity.
One uint8_t generation per slot makes ordinary clear independent of the table
size. Every 255 clears the generation array is zeroed. Tests cross this wrap
and reject batches over stale colliding slots with byte-exact restoration.
`INPUT_INDEX_THRESHOLD=16` selects the regime from capacities at init. Tests
exercise D=15/16 and both the 16-byte text linear regime (D=8) and 128-byte text
indexed regime, including cross-role deduplication, byte-exact rejection and
reuse with allocation disabled. The SDK consumer stays at 128 text bytes.

## Stratified aggregates

`test_maelys_datalog_count` runs in both native profiles and sanitizers; CMake
registers it as `stratified_count`. It covers typed distinct projection, explicit
empty groups, global and policy-fact counts, positive recursive sources,
negation, multiple aggregates, scope/stratification rejections, custom frontend
round trips and malformed IR, old planner callbacks, and rejection of a backend
without aggregate capability before its prepare callback. A host-side oracle
checks 100 successive snapshots against integer sets after source filtering.
Capacity rejection and session reuse never publish partial results. Explanation
checks cover order independence, short-output retries and count mismatches.

The numeric cases additionally check complete-tuple sum deduplication, empty
extrema, nonmatching heterogeneous groups, integer boundaries, overflow/type
rejection and reuse, frozen recursive IDB sources, separate capability gates
and malformed IR. A second host-side oracle checks 100 snapshots against
complete tuple sets for all three numeric operators.

The whole-engine allocator guard solves and explains all aggregate snapshots with the
allocator disabled and a configured explanation workspace. The bounded
projection buffer contains `max(MAX_RULE_FACTS, MAX_FACTS_PER_PRED)` terms:
2 KiB SMALL / 4 KiB LARGE on targets with 16-byte native terms. It is local
to one evaluation and returns before rule traversal continues; there is no
per-group allocation or persistent aggregate cache. Existing explanation
premise size is statically preserved. CFFI and JavaScript allocations are not
covered by the engine's zero-allocation claim.

The Python binding and the Node/Wasm playground exercise group counts, zero,
explanations and (where sessions are exposed) snapshot reuse. Existing pipeline
goldens continue to constrain identities and non-aggregate explanation bytes.
These functional/allocation checks establish no speed improvement; apply the
manual comparison protocol above to changes in ordinary solve paths.

## Installed facade and SDK

```sh
cmake -S . -B build/cmake
cmake --build build/cmake --parallel 3
ctest --test-dir build/cmake --output-on-failure
bash tools/check_module_sdk.sh "$PWD/build/cmake"
bash tools/check_sdk_archive.sh "$PWD/build/cmake"
```

Repeat in `build/cmake-large` with `-DMAELYS_DATALOG_PROFILE_LARGE=ON`.
The SDK check installs into a fresh temporary prefix, copies all consumers and
providers outside the source tree and builds them with only installed includes
and libraries, both static and shared. It checks all ten headers independently
as C11/C++17 and rejects `sizeof` on all twelve opaque handle types. All five
standalone examples (including the frontend/filter bundle) are copied out, built
with their own CMake projects and run through the installed conformance kit in
both linkage modes and size profiles.

The archive gate invokes the release's `package-native-sdk.sh` against that
same build, extracts the real tarball into a fresh directory, and compares its
headers, library, SDK support files and licenses with CMake installation.
It reuses the external SDK consumers, examples and starters with static linkage,
checks the profile through `limit_get`, and rejects historical/private includes
in C11/C++17. The source public-header directory supplies the expected inventory,
so forgetting a new header in the sole CMake list also fails. Neither consumer
build inherits ambient include/library search paths.

Negative controls remove `datalog_details.h` and inject the historical aggregator
into the extracted copy; both must make the validator fail, with the relevant
filename in its diagnostic. A format-level check also compares raw tar members
with extracted paths: BSD tar can silently absorb AppleDouble metadata, so its
own listing is insufficient. Packaging disables creation of those host metadata
entries. A third negative control duplicates an identical tar member, which
leaves the extracted content unchanged but must fail the raw-inventory check.
These controls never modify the source or the original archive.
On macOS, CMake's post-install `ranlib` is scoped to `ZERO_AR_DATE=1` and the
archive gate requires a zero symbol-index timestamp. This makes byte parity
independent of whether the two installations happen in the same second. Both
SMALL and LARGE run in the existing SDK CI jobs. Release packaging runs the gate
on its actual SMALL artifact before writing checksums/receipts. These tests do
not establish performance or change the engine's allocation guarantees.

The same check copies the four MIT starters from the installed prefix, verifies
their packaged LICENSE files and MIT identifiers, then builds/runs each standalone
project in Release mode with static/shared linkage. Their checks exercise
registration and explicit `UNSUPPORTED` callbacks, not solver/filter correctness.
The starters do not include the MPL conformance helper or private engine headers.

The public consumer fixture proves result-scoped symbol authority by
construction rather than by a mutant build: its domain declares no atoms and
its policy no constants, so the only symbol it resolves through
`maelys_datalog_result_symbol_text` exists solely in the solved EDB. A runtime
that consulted the prepared policy's symbol table instead of the result's
working table cannot pass that lookup (exit 9).

## Actual Python and WASM wrappers

| Gate | Verified behavior |
| --- | --- |
| `test_maelys_datalog_predicate_builders` | All six origin/query flag mappings, static and dynamic initializers, single evaluation, query permissions, query-only rejection and policy-fact input rejection; C11/C++17 compilation is also covered by the fact-builder and installed-SDK gates. Python constructor tests exercise the same origins and retain subclass/immutability checks. |
| `test_maelys_datalog_query_builders` | Query arities 0–4, typed API parity, integer bounds, Boolean/integer distinction, unchanged output on errors versus successful absence, single evaluation, borrowed symbol initializers. |
| `test_maelys_datalog_input_edb_alloc` | Caller-owned alignment/size, copied and shared strings, byte-for-byte atomic rejection, fixed capacities and allocation-free append/clear. |
| `test_maelys_datalog_hot_path_alloc` | All engine units use allocator hooks: repeated reference append/solve/query/release without allocator calls, constructor allocation failures, independent sessions and failure recovery. A source-level `memset` hook checks zero reset bytes on owned native release and at most 4,096 on reusable public release, on both profiles; this is not a hardware store counter or secure-erasure guarantee. Configured aggregate explanations are exercised with allocation disabled. Empty successful public solves must request fewer than 32,768 memset bytes across the engine; rejected transactions retain their full cleanup. |
| `test_maelys_datalog_pipeline` | Existing fingerprint/proof goldens and identical result symbol IDs under input permutation. |
| C11 cases in `test_maelys_datalog_input_edb_alloc` | Unit/batch arity 0–4, integer ranks, copied strings, typed/explicit booleans, exactly-once arguments, multi-digit fact/term range diagnostics, and byte-identical late range/type/text/fact-capacity rejection with the allocator disabled. |
| `make check-c11-fact-builders` / CTest `c11_fact_builder_compilation` | Strict C11 unit/batch/query consumers; float, double, pointer, struct and five-term compilation failures for each macro. The Make gate additionally checks C++17 symbol/predicate initializers and absence of C11-only macros; CMake keeps its C-only compiler requirement. |
| `tests/python` | Public-header-only CFFI, native atomic input, limits/diagnostics, manifest/domain isolation, prepared sessions, explicit reset, one-result lease, filters and Why-true/Why-false truncation. Compiled outside the checkout against an installed SDK. Expected facts/text and V1 migration contracts replace cross-binding parity; no missing-extension skip. |
| `tests/python/test_prepared_explanations.py` | Default calls prepare once in CFFI-owned storage and release in finally. Opt-in sessions use two direct-text calls without allocating a CFFI arena per explanation; alternating kinds preserve default output. Mask validation, unreserved-kind rejection, output allocation/write/UTF-8 decoding failures, result close and session reuse are exercised. Python/CFFI still allocate. |
| `test_maelys_datalog_session_explanations` | All engine units use allocator hooks. Owned/borrowed session workspaces, one extra allocation at initialization, constructor failures, overlapping live ranges, mask replacement, default behavior and custom-descriptor rejection. Forty alternating TRUE/FALSE measure/query/short-write/retry cycles per mode make no allocator calls and prepare once per query. The runtime is included only in this test to instrument the canonical backend's callback, not to allow a copied backend to claim its bound. Value/kind/predicate/result-generation cache keys, failure recovery and explicit versus internal result leases are checked. |
| `test_maelys_datalog_prepared_explanations` | All engine units use allocator hooks; Why-true/Why-false prepared, one-shot and session-cached paths run with allocation disabled and preserve identical text, including both truncated kinds. One-shot short storage preserves the required-length output; short text reports its length, retry rebuilds, and write failures release the lease. Per-kind reference bounds dominate exact sizes; copied descriptors are refused. Repeated prepared writes call no preparation callback; result leases, alignment, storage reuse, unknown symbols, invalid callbacks and ABI 2 rejection are checked. |
| `test_maelys_datalog_why_false` | Every successful legacy fixture is compared byte-for-byte with the caller-owned workspace path, including recursion, reordered symbol vocabularies, filters and truncation budgets. |
| `test_maelys_datalog_modules` | Filter validation, cost and evaluation callbacks, and planner callbacks, preserve `STORAGE_TOO_SMALL`; failed solves expose no result and the session remains reusable. |
| `check_module_sdk.sh` | All eleven opaque handle layouts rejected in C11/C++17; static/shared external consumers exercise caller-owned explanations, configured owned/borrowed workspaces, `STORAGE_TOO_SMALL` and result leases. |

Run the sanitizer build used by CI, not only CMake's separate targets:

```sh
make -j4 -f Makefile.asan asan PROFILE=SMALL
make -j4 -f Makefile.asan asan PROFILE=LARGE
```

Ordinary tests share ASan objects. The input allocator test excludes the input
implementation object it already includes; the whole-engine allocator tests share
one separate guarded object set. macOS runs ASan/UBSan with LSAN disabled; Linux also
runs LSAN. Custom callbacks/backends, Python/CFFI and libc internals are not
covered by the reference-engine no-allocation assertion.

The single Python binding’s build/test commands, migration table and lifecycle
examples are in its [README](../bindings/python/README.md); test SMALL and LARGE.

The WASM C boundary and JavaScript wrapper live together in
[`bindings/wasm/`](../bindings/wasm/README.md). Generated modules remain under `build/wasm` and `build/wasm-large`. The gate
copies the built wrapper and tests outside the checkout before execution.

After each profile’s CMake build, run the isolated installed-SDK Python gate:

```sh
python3 -m venv build/validation-venv
build/validation-venv/bin/python -m pip install cffi setuptools pytest
bash tools/check_python_binding.sh "$PWD/build/cmake" build/validation-venv/bin/python small
bash tools/check_python_binding.sh "$PWD/build/cmake-large" build/validation-venv/bin/python large
```

The gate installs a fresh SDK, copies binding/tests outside the checkout, clears
ambient C include/library search paths and compiles CFFI using only that prefix.
Only `libmaelys_datalog_shared` is copied next to the extension; the native-object
shim is removed. Each profile starts fresh Python processes and checks loaded
limits through `MAELYS_DATALOG_EXPECT_PROFILE`. New inodes avoid stale Mach-O
signature pages when switching builds. Ground-query origins, exact Why-true
text, truncation, raw-term ownership, domain conflict/reuse, and intentional V1
API/lifecycle changes are covered alongside the former Python-next suite.
`test_sdk_admission.py` preserves the current layouts but changes the SDK API
version to 1 or 3 in independent copies: compilation must fail at the explicit
API 2 guard. Removing that guard makes both negative controls fail.

```sh
for profile in small large; do
  make -B -f Makefile.wasm maelys_datalog_dynamic.js WASM_PROFILE="$profile" EM_CACHE="$PWD/build/emscripten-cache"
  bash tools/check_wasm_binding.sh "$profile"
  EM_CACHE="$PWD/build/emscripten-cache" bash tools/check_wasm_extensions.sh "$profile"
done
```

These run actual compiled Wasm under Node, not a browser. No backend selector
is added to either binding by this validation work.
The extension check separately links each C example and the C host into the same
WASM module. It runs all five conformance executables under Node; it does not use
side modules or alter the shipped JavaScript wrapper.

CI runs both profiles with Emscripten 3.1.61 (the release toolchain) and
4.0.14. The builder installs an Emscripten static SDK in a fresh prefix and
compiles the adapter outside the source tree using only its public headers.
API 1/3 and private-header negative controls must fail. A separate C consumer
compares results and canonical explanation bytes with the public SDK; native
ASan/UBSan and allocation guards also exercise malformed frames, rollback,
int64 extremes, short-output retry, close and reuse. The actual Wasm export
table must contain the binding allowlist and no historical native exports.
Node tests cover typed inputs, all aggregate operators, atom authority, result
leases, structured capacity diagnostics, both explanation kinds/truncation,
exact int64, copied values, multi-predicate atomicity and memory-view growth.
No timing or whole-binding zero-allocation claim follows from these tests.

## Bounded robustness smoke and guards

```sh
make -f Makefile.fuzz fuzz-smoke
make -f Makefile.afl afl-smoke
bash tools/check_module_boundaries.sh
make check-version-header
actionlint
git diff --check
```

The existing libFuzzer smoke is bounded to 30 seconds / 10,000 runs; the AFL++
smoke is bounded to 30 seconds. Both use build-directory corpus copies.
Accepted inputs now also run against a small typed EDB. The deterministic corpus
uses the same bounded fixture. The solver's test-only linear oracle checks every
candidate and the fixture requires no base hits or production base lookups for
these unmodified validated programs. This does not enumerate all bindings.
They are regression smokes, not exhaustive
coverage, a security certification, or a campaign to develop exploits.
The harness rulesets are zero-initialized and preparation failures abort the
smoke instead of silently skipping every input. On macOS, the existing LLVM
libFuzzer target uses `ld_classic` (with a linker deprecation warning); the
Linux AFL++ variant is available through `make -f Makefile.afl afl-smoke-linux`.

CI runs this matrix on Linux/macOS for native/SDK/Python, and on Linux for
WASM and fuzz. Its verdict is separate from independent engineering acceptance.


## Common application API migration

`test_maelys_datalog_advanced` covers diagnostic boundary sentinels and old
callback rejection, deferred domain installation and sticky errors, structured
positive/negative and aggregate views, result leases, caller-owned policy
storage/reuse, multi-policy buffer loading and retained-context configuration.
It is also compiled outside the source tree against the installed static and
shared SDK. `test_maelys_datalog_diagnostic_export` checks independent simultaneous
capacity/predicate/depth/rule details without enlarging the compact solver
working diagnostic. Prepared-explanation allocation guards exercise the new
structured accessors and filter statistics with all engine allocators disabled.
The SDK checks include the new headers in C11 and C++17. Legacy CFFI consumers
are recompiled; Python-next initializes the size/version protocol explicitly.

`test_maelys_datalog_diagnostic_writes` instruments actual reset calls and memset
bytes through both public solve entries, verifies that text tails remain intact,
and checks that invalid versions cause no writes. The advanced SDK consumer
provokes per-predicate and global IDB exhaustion through real solves, checks
capacity/predicate sections together, then reuses each session successfully.

Prepared-session reuse tests poison inactive fact/index/pointer payload, alternate
full duplicate-heavy symbolic batches, indexed integer batches, tiny and empty
inputs, and compare results/proofs byte-for-byte with the independent fresh EDB
construction path. Borrowed-pointer cleanup covers the live scratch beyond the
index. Three consecutive integer-only transactions (40, 40, then 60 distinct
facts) exercise index reuse without a symbol-pointer cleanup between them; each
must derive exactly the current values and match the fresh facts/proofs oracle.
Late rejection restores empty facts/scratch and the exact prepared symbol
table, and a subsequent solve succeeds. Normal success only resets metadata and
the scratch span needed to drop pointers and initialize the index; inactive facts
are retained until overwritten, with no secure-erasure guarantee or new storage.

## Native archive reproducibility

`python3 tools/check_native_reproducibility.py SMALL` (or `LARGE`) builds the
same sources in two distinct checkout/build paths with different file mtimes,
then compares the static libraries and entire SDK tarballs byte for byte.
It requires retained canonical debug paths and rejects leaked temporary paths.
Both profiles run on Linux and macOS in the SDK jobs.

Native release packaging enables `MAELYS_DATALOG_REPRODUCIBLE_PATHS` explicitly;
normal developer builds retain their paths. Source and build paths (including
macOS canonical aliases) map to `/maelys-datalog` and `/maelys-datalog-build`.
Debuggers can substitute these paths to local sources. Debug information remains
present. The archive sorts members, normalizes owner/mode/mtime, excludes host
xattrs and fixes the gzip timestamp/name. `SOURCE_DATE_EPOCH` defaults to the
source commit time. Reproduction requires identical sources, compiler, SDK,
build flags and compression toolchain; this is not a cross-toolchain guarantee.
Release receipts and attestations describe each execution and are not made
byte-identical. Wasm packaging is outside this native correction.

## Aggregate rejection diagnostics

The allocator-disabled aggregate test rejects negative and out-of-range int64
values, booleans and symbols for min/max/sum. It checks the category, source
predicate, argument index, exact numeric token and bound, and then reuses the
session. A sum of valid operands 2147483647 and 1 has a separate overflow code
and token 2147483648. The diagnostic is exported before rollback restores the
symbol dictionary, so runtime symbols are copied while their IDs remain valid.
Both failure classes return INVALID_FIELD without publishing a result.

`DIAGNOSTIC_AGGREGATE` interprets existing fields: `field` is the operator,
`token` is the value text, `lhs_kind` uses IR term kinds, `term_index` is the
zero-based source projection and `limit` is 2147483647. Overflow reports the
first partial sum crossing the bound, not a total computed after rejection.
This is a numeric bound, not a CAPACITY section or a limit_get selector. Symbol
text is bounded by token storage; integer decimal text is always exact.
Public layout/version and backend ABI are unchanged. Private unions only reuse
mutually exclusive error payloads, with size/alignment/offset assertions; public
sections remain independently present. Python and Wasm test the same failures
through installed SDKs, including the second/third argument projection.
