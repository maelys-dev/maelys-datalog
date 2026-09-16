# Validation matrix

Run suites sequentially. Separate SMALL/LARGE build directories prevent a stale
binary from silently exercising the other profile. Generated build artifacts are
not proof that tests ran; check the exit status and the suite summaries.

## Native C and sanitizers

```sh
make -j3
make -j3 test
make -j3 test BUILD_DIR=build/native-large CFLAGS='-Wall -Wextra -g -I. -Iinclude -DMAELYS_DATALOG_PROFILE_LARGE'
make -j3 -f Makefile.asan asan
make -j3 -f Makefile.asan asan BUILD_DIR=build/asan-large CFLAGS='-Wall -Wextra -g -I. -Iinclude -DMAELYS_DATALOG_PROFILE_LARGE'
make bench-pipeline
```

Both Make test inventories use `tests/test_*.c`, including the policy-set
fingerprint suite. `asan` reruns every test even if its binary is up to date.
The current inventory has 32 executables: the existing 622 framework cases,
12 module cases, 11 compiler/backend cases and 27 pipeline checks (672), plus
the context suite for atomic registration, concurrent isolation, retained
lifetimes, named selection, invalid planner output, capacity and fingerprints.
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
as C11/C++17 and rejects `sizeof` on all six opaque handle types. All five
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

The experimental `bindings/python-next` wrapper includes only public
`maelys/datalog.h`. A test rejects backend/IR headers in the generated bridge.
Build its shared library and CFFI extension from the same checkout:

```sh
cmake -S . -B build/python-next-small -DBUILD_TESTING=ON
cmake --build build/python-next-small --parallel 3
ctest --test-dir build/python-next-small --output-on-failure
python3 bindings/python-next/build_cffi.py --build-dir build/python-next-small
MAELYS_DATALOG_EXPECT_PROFILE=small PYTHONPATH=bindings/python-next \
  python3 -m unittest discover -s bindings/python-next/tests -v
```

Repeat in a distinct build directory with `-DMAELYS_DATALOG_PROFILE_LARGE=ON`
and `MAELYS_DATALOG_EXPECT_PROFILE=large`. The gates exercise buffered single and
batch additions, rejected-batch atomicity, original-input diagnostic indices,
loaded-library limits, non-queryable derived counts and result lifetime. They
also exercise manifest SHA/query validation, policy-local atoms without registry
leakage, multi-policy indices, full diagnostics, prepared A/B/A reuse and retry,
Why-true/Why-false (including truncation), ground filters, result-owned raw terms,
creating-thread enforcement and distinct authority/execution fingerprints.
Explicit work budgets are tested as UNSUPPORTED on the reference backend,
not falsely advertised as enforced. The
parity test requires the existing Python extension below; when it is absent,
that test reports a skip. The installed SDK consumer also calls the public
limit/count getters in both static and shared linkage. Native facade tests
cover corrected-input retries after predicate, term, symbol and capacity errors.

Opaque session configuration tests cover zero defaults, rejected unknown bits
without mutation, NULL arguments and unchanged getter outputs, independent
session snapshots after config mutation/free, and default fingerprint parity.
The installed consumer uses only `datalog.h` to configure and solve; it fails
compilation if that header pulls in backend/IR/module declarations. The SDK
gate rejects `sizeof(session_config)` in both C11 and C++17, alongside the
other opaque handles. Existing extension consumers still use `session_create_ex`.

The owned input EDB gates check copied caller strings, typed scalar values,
atomic invalid batches, entry bounds before deduplication, UTF-8 byte limits,
clear/retry, result independence after buffer clear/free, and session result
leases. Python tests additionally exercise failed iterators, iterator-triggered
closure, parent cleanup, successful-solve mutation freeze, and the new earlier
storage errors. The installed SDK exercises the EDB in static/shared linkage;
its layout must remain incomplete in C11 and C++17. No persistent Python fact
list is kept. ASan/UBSan can run the same native tests; macOS does not support
LeakSanitizer, so disabling LSAN there is not evidence of leak checking.

### Allocation contract (opaque input path)

`input_edb_storage_requirements(fact_capacity, text_capacity, ...)` reports
the byte size and alignment required by the loaded library. `input_edb_init`
uses caller-owned storage; its lifetime spans init through free, and inputs,
outputs and diagnostics must not overlap it. Capacities never grow. Copied
predicate/symbol bytes include one NUL per distinct byte string. Repeated strings
share storage, including repeats within a batch and across predicate/symbol roles.
Clear reuses the entire reserve; it does not securely erase old data.

| Path | Allocation contract |
| --- | --- |
| Input EDB requirements/init, append, count, clear, caller-owned free | No allocator calls. Capacity failures never fall back to heap. |
| Input EDB create/create_with_capacity | One allocation at creation, one free at destruction; no later growth. |
| Opaque solve input conversion / canonical export | Bounded scratch reused inside the session; no separate per-solve allocation. |
| Reference/prepared session solve, query and result release | No engine allocator calls after initialization. Native/public results and provenance are reserved per session. |
| Policy/session creation, legacy standalone solve_once, on-demand explanations | May allocate. Session construction fails if its result workspace cannot be reserved. |
| Python Next | Native input storage follows the constructor contract; Python/CFFI still allocate temporary objects. |

The default input constructor reserves the profile's maximum fact count plus
`INPUT_EDB_TEXT_BYTES` of text. This budget is the native 32 KiB symbol pool plus
8 KiB of registry-name storage (128 names of up to 63 bytes and their NULs), not
worst-case repeated strings per fact. Query it with `limit_get`; explicit budgets
can be smaller, never larger. Shape/string/text-capacity checks happen at insertion;
the domain and its symbol-count/pool/per-predicate limits are still checked at solve.
The input buffer retains logical facts independently of any policy, not public
native symbol IDs. Conversion into the session's native EDB remains necessary to
assign IDs canonically, independent of insertion order, using preallocated scratch.

`test_maelys_datalog_input_edb_alloc` disables and counts the input allocator,
checks aligned/undersized/misaligned storage and overflow, checks byte-for-byte
unchanged arenas after invalid batches or exhausted text capacity, and exercises
repeated clear/reuse. Its build rejects calloc/realloc in the input implementation.
It proves input-buffer behavior only, not allocation freedom inside libc or the
solver. The installed SDK consumer uses caller-owned input storage with C11 and
C++17 and static/shared linkage, without exposing private layouts.

`test_maelys_datalog_hot_path_alloc` recompiles all engine units with allocator
hooks, refuses allocations during repeated append/solve/query/release cycles,
checks independent sessions and input/solver failure recovery, and injects failure
at every session-construction allocation. Explanations are explicitly tested
outside that guard. Hot-path sorting uses an in-place nonrecursive heapsort rather
than libc qsort, which is permitted to allocate. The test does not interpose libc
internals or promise allocation freedom in custom backend/filter callbacks.

The result is a lease on reserved session storage, not an independently allocated
object. No second solve or session destruction is permitted while it is live.
Release invalidates the handle even if its address is reused later. EDB clear/free
does not change that live result. Why-true/Why-false workspaces requested afterward
remain bounded, temporary allocations; provenance collected during solve is already
reserved. Legacy standalone `solve_once` remains an allocating convenience API.

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
