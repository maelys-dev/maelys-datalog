# Maelys Datalog Python binding

This directory contains the native Python binding for Maelys Datalog. It uses
cffi in out-of-line mode and a small C shim (`maelys_py_bind`) over the public C
API. The engine C API remains the source of truth.

## Python compatibility

The wrapper supports Python 3.10 and later. Its public typing aliases are
exported from `maelys_datalog`: `InputTerm`, `ResolvedTerm`, `Fact`, and
`RawFact`.

## Build

In a fresh Python environment, install the build and test tooling first:

```sh
python3 -m pip install cffi setuptools pytest
```

Build the native shared libraries with CMake:

```sh
cmake -S . -B build/python-small -DMAELYS_DATALOG_BUILD_PYTHON_BINDING=ON
cmake --build build/python-small --target maelys_py_bind
```

Then build the cffi extension:

```sh
python bindings/python/build_cffi.py
```

The build copies `libmaelys_datalog_shared` and `libmaelys_py_bind` next to the
Python package so the extension can load them through `$ORIGIN` / `@loader_path`
without `LD_LIBRARY_PATH` or `DYLD_LIBRARY_PATH`.

Set `MAELYS_DATALOG_PROFILE_LARGE=ON` in the CMake configure step to build the
LARGE profile. `Engine().limits` exposes the active build limits and is the
recommended way to verify which profile is currently loaded.

## Domain registration

`Engine.register_domain()` registers predicate declarations only. It does not
register atom allowlists; policies loaded through this binding should not rely on
string constants in `.dl` source unless a separate native domain already
registered those atoms. Prefer qualitative boolean predicates for low-cardinality
categories, for example `window_direction_in(W)`.

The native domain registry is process-wide and has no eviction API. Reuse a
stable `domain_name` across notebook sessions, service hot-reloads, and
long-running monitoring processes. Generating a new domain name for every logical
reload will eventually exhaust the process-wide registry.

The Python wrapper serializes domain registration and inline ruleset loading with
a process-wide lock because cffi releases the GIL during calls into C.

Each `Ruleset` also owns a reentrant lock that serializes symbol interning,
symbol resolution, and `Edb.add_fact()` for distinct EDBs sharing that ruleset.
This does not make a shared `Ruleset`, `Edb`, or `SolveResult` generally
thread-safe, and concurrent solves on one ruleset are not a documented
guarantee. Do not call `close()` concurrently with use of a dependent object.

`SolveResult.enumerate_predicate_facts()` returns resolved `Fact` tuples.
`enumerate_predicate_facts_raw()` returns `RawFact` tuples of `Term` values and
does not resolve symbol ids to text.

## EDB finalization

`Ruleset.solve(edb)` finalizes the EDB exactly once before calling the native
solver. After a successful solve, the Python `Edb` object is closed for mutation:
subsequent `add_fact()` calls raise `RuntimeError` before reaching C. Calling
`solve()` again on the same finalized EDB is allowed and reuses the finalized
snapshot.
