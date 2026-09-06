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
The current inventory has 31 executables: 622 framework cases, 12 module cases,
11 compiler/backend cases, and 23 pipeline checks (668 total per profile).
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
check the same 21 transcript goldens and two filter validations. The benchmark's
CPU time is descriptive, not a throughput guarantee.

## Installed facade and SDK

```sh
cmake -S . -B build/cmake -DMAELYS_DATALOG_BUILD_PYTHON_BINDING=ON
cmake --build build/cmake --parallel 3
ctest --test-dir build/cmake --output-on-failure
bash tools/check_module_sdk.sh "$PWD/build/cmake"
bash tools/check_public_authority.sh "$PWD/build/cmake"
```

Repeat in `build/cmake-large` with `-DMAELYS_DATALOG_PROFILE_LARGE=ON`.
The SDK check installs into a fresh temporary prefix, copies all consumers and
providers outside the source tree and builds them with only installed includes
and libraries, both static and shared. It checks all four headers independently
as C11/C++17 and rejects `sizeof` on all five opaque handle types.

The authority check first runs the unchanged public consumer. It then builds
one temporary runtime unit selecting the prepared policy's symbol table instead
of the result's working table. The consumer must reject that mutant precisely
at result symbol lookup (exit 9); a compiler failure or crash does not qualify.
The normal source tree/library is never modified by this check. Temporary
consumer/mutant artifacts are removed after each check.

## Actual Python and WASM wrappers

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
CTest's C shim test is complementary,
not a replacement for pytest.

```sh
for profile in small large; do
  make -B -f Makefile.wasm maelys_datalog_dynamic.js WASM_PROFILE="$profile" EM_CACHE="$PWD/build/emscripten-cache"
  for script in tests/wasm/*.mjs; do
    MAELYS_WASM_PROFILE="$profile" node "$script"
  done
done
```

These run actual compiled Wasm under Node, not a browser. No backend selector
is added to either binding by this validation work.

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
