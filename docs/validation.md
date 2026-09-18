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

## Installed facade and SDK

```sh
cmake -S . -B build/cmake -DMAELYS_DATALOG_BUILD_PYTHON_BINDING=ON
cmake --build build/cmake --parallel 3
ctest --test-dir build/cmake --output-on-failure
bash tools/check_module_sdk.sh "$PWD/build/cmake"
```

Repeat in `build/cmake-large` with `-DMAELYS_DATALOG_PROFILE_LARGE=ON`.
The SDK check installs into a fresh temporary prefix, copies all consumers and
providers outside the source tree and builds them with only installed includes
and libraries, both static and shared. It checks all five headers independently
as C11/C++17 and rejects `sizeof` on all nine opaque handle types. All five
standalone examples (including the frontend/filter bundle) are copied out, built
with their own CMake projects and run through the installed conformance kit in
both linkage modes and size profiles.

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
| `test_maelys_datalog_hot_path_alloc` | All engine units use allocator hooks: repeated reference append/solve/query/release without allocator calls, constructor allocation failures, independent sessions and failure recovery. A source-level `memset` hook checks zero reset bytes on owned native release and at most 4,096 on reusable public release, on both profiles; this is not a hardware store counter or secure-erasure guarantee. Explanations are outside the allocation guard. |
| `test_maelys_datalog_pipeline` | Existing fingerprint/proof goldens and identical result symbol IDs under input permutation. |
| C11 cases in `test_maelys_datalog_input_edb_alloc` | Unit/batch arity 0–4, integer ranks, copied strings, typed/explicit booleans, exactly-once arguments, multi-digit fact/term range diagnostics, and byte-identical late range/type/text/fact-capacity rejection with the allocator disabled. |
| `make check-c11-fact-builders` / CTest `c11_fact_builder_compilation` | Strict C11 unit/batch/query consumers; float, double, pointer, struct and five-term compilation failures for each macro. The Make gate additionally checks C++17 symbol/predicate initializers and absence of C11-only macros; CMake keeps its C-only compiler requirement. |
| `bindings/python-next/tests` | Public-header-only CFFI, native atomic input, limits/diagnostics, manifest/domain isolation, prepared sessions, explicit reset, one-result lease, filters and Why-true/Why-false truncation. Parity is mandatory with `MAELYS_DATALOG_REQUIRE_PARITY=1`; otherwise it may skip when the legacy extension is absent. |
| `bindings/python-next/tests/test_prepared_explanations.py` | Each explanation prepares once in aligned CFFI-owned storage, reads the cached size and writes text without calling the legacy direct-text functions. Native failure, Python allocation failure and UTF-8 decoding failure release every successfully prepared handle; closing the result and reusing its session prove the lease is released. Python/CFFI still allocate. |
| `test_maelys_datalog_prepared_explanations` | All engine units use allocator hooks; Why-true/Why-false prepare/size/write/release run with allocation disabled. Repeated writes call no preparation callback; result leases, alignment, short buffers, storage reuse, unknown symbols, invalid callbacks and ABI 2 rejection are checked. |
| `test_maelys_datalog_why_false` | Every successful legacy fixture is compared byte-for-byte with the caller-owned workspace path, including recursion, reordered symbol vocabularies, filters and truncation budgets. |
| `check_module_sdk.sh` | All nine opaque handle layouts rejected in C11/C++17; static/shared external consumers exercise caller-owned explanations and result leases. |

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

Python-next's executable build/test commands and lifecycle examples are in its
[README](../bindings/python-next/README.md); run both SMALL and LARGE builds.

The WASM C boundary and JavaScript wrapper live together in
[`bindings/wasm/`](../bindings/wasm/README.md). Tests import the wrapper from
there; generated modules remain under `build/wasm` and `build/wasm-large`.

After each profile's CMake shim build, rebuild cffi and run a fresh Python process:

```sh
python3 -m venv build/validation-venv
build/validation-venv/bin/python -m pip install cffi setuptools pytest
bash tools/check_python_binding.sh "$PWD/build/cmake" build/validation-venv/bin/python small
bash tools/check_python_binding.sh "$PWD/build/cmake-large" build/validation-venv/bin/python large
```

The shim copies its two native libraries next to the Python package. Do not
rely on an up-to-date CMake build to copy them again: `POST_BUILD` will not run.
The check script explicitly selects the libraries from the requested build and
sets `MAELYS_DATALOG_EXPECT_PROFILE`: the test checks the loaded limits.
Libraries are copied to fresh files and renamed into place; overwriting an
already loaded Mach-O inode can trigger macOS code-signature page-cache kills
when changing profiles, even if `codesign --verify` reports valid files on disk.
CTest's C shim test is complementary,
not a replacement for pytest.

```sh
for profile in small large; do
  make -B -f Makefile.wasm maelys_datalog_dynamic.js WASM_PROFILE="$profile" EM_CACHE="$PWD/build/emscripten-cache"
  for script in tests/wasm/*.mjs; do
    MAELYS_WASM_PROFILE="$profile" node "$script"
  done
  EM_CACHE="$PWD/build/emscripten-cache" bash tools/check_wasm_extensions.sh "$profile"
done
```

These run actual compiled Wasm under Node, not a browser. No backend selector
is added to either binding by this validation work.
The extension check separately links each C example and the C host into the same
WASM module. It runs all five conformance executables under Node; it does not use
side modules or alter the shipped JavaScript wrapper.

CI runs both profiles with Emscripten 3.1.61 (the release toolchain) and
4.0.14. Allocation-instrumentation tests resolve lazy `_malloc`/`_free`
exports before installing hooks; warm-up allocations are outside the
measured operations and injected failures. No production wrapper warm-up
or relaxed allocation assertions are needed.

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
They are regression smokes, not exhaustive
coverage, a security certification, or a campaign to develop exploits.
The harness rulesets are zero-initialized and preparation failures abort the
smoke instead of silently skipping every input. On macOS, the existing LLVM
libFuzzer target uses `ld_classic` (with a linker deprecation warning); the
Linux AFL++ variant is available through `make -f Makefile.afl afl-smoke-linux`.

CI runs this matrix on Linux/macOS for native/SDK/Python, and on Linux for
WASM and fuzz. Its verdict is separate from independent engineering acceptance.
