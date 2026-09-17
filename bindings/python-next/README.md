# Python Next — experimental opaque-facade binding

`python-next` is a parallel experiment, **not a replacement** for
`bindings/python` and not a published package. It imports as
`maelys_datalog_next`, so the existing `maelys_datalog` import is untouched.

The generated CFFI extension includes only public `<maelys/datalog.h>` and links
`libmaelys_datalog_shared`. There is no hand-written second C shim and no
`src/core/` include. The package exercises the opaque facade's domain → policy →
session → facts → solve → query path, including additive public limit/count
getters introduced alongside this experiment. Build it with the native library
from a matching development checkout including caller-owned prepared explanations;
older libraries lacking these entry points cannot build this updated binding.
The Python signatures stay compatible; this is not binary compatibility with an
older native library.

The facade now owns the capability constants, opaque `session_config` handle
and execution fingerprint accessor. `Ruleset.prepare()` creates a temporary
configuration, sets its requirements, calls `session_create_configured()` and
frees the configuration in a `finally` block. The native session copies its
values; it does not borrow that temporary handle. No backend descriptor,
`struct_size`, backend ABI number or public IR declaration is needed by Python.
The Python method signatures and behavior are unchanged. Native extension
authors keep `datalog_backend.h` and `session_create_ex()` for custom backends.

## Build

Python 3.10+, CFFI, a C11 compiler, and CMake 3.16+ are required.

```sh
cmake -S . -B build/python-next
cmake --build build/python-next --target maelys_datalog_shared
python bindings/python-next/build_cffi.py --build-dir build/python-next

PYTHONPATH=bindings/python-next python -m unittest discover \
  -s bindings/python-next/tests -v
```

The parity test also uses `bindings/python` if its CFFI extension has been
built; otherwise that one test is explicitly skipped. A broken installed
extension or ABI mismatch fails rather than silently skipping parity. The two
bindings run in separate Python processes: native libraries from different
revisions may share an install name, so importing both in one interpreter can
select the wrong library. Do not mix mismatched native builds in one process.
To test the LARGE profile, configure a separate CMake directory with
`-DMAELYS_DATALOG_PROFILE_LARGE=ON`, rebuild CFFI against that directory, and
run with `MAELYS_DATALOG_EXPECT_PROFILE=large`. For SMALL use
`MAELYS_DATALOG_EXPECT_PROFILE=small`; this verifies the library actually loaded,
not merely the headers used at compile time.

`--engine-dir` is a development-only build option, not a runtime setting or a
package installation interface. The normal build above uses this checkout.
When the native changes and Python changes are in separate development worktrees,
select the **same engine checkout** for the public header and its library:

```sh
python bindings/python-next/build_cffi.py \
  --engine-dir /absolute/engine-checkout --build-dir build/cmake-small
MAELYS_DATALOG_ENGINE_DIR=/absolute/engine-checkout \
MAELYS_DATALOG_EXPECT_PROFILE=small PYTHONPATH=bindings/python-next \
  python -m unittest discover -s bindings/python-next/tests -v
```

With `--engine-dir`, a relative `--build-dir` is relative to that checkout.
The test variable selects its header for the public-surface coverage check; it
does not change which library Python loads. Rebuild CFFI when switching builds.

CI sets `MAELYS_DATALOG_REQUIRE_PARITY=1`: a missing legacy extension fails
instead of skipping. It builds/tests legacy SMALL, then Python-next SMALL,
then legacy LARGE and Python-next LARGE, on Linux and macOS. The legacy
extension must remain importable by the same Python interpreter throughout.

`build_cffi.py` copies the shared native library and writes the compiled CFFI
module into the **source directory** `bindings/python-next/maelys_datalog_next/`;
temporary compilation files live in `bindings/python-next/build/`. These
generated paths are covered by the repository's `.gitignore`. This in-tree
build is an experimentation choice, not a package publication mechanism or
a wheel installation strategy. On a CI runner, build both bindings
against the **same** source commit and size profile; this experiment does not
yet have package or release integration.

## Example

```python
from maelys_datalog_next import Engine, Predicate

predicates = [
    Predicate.edb("user", 1),
    Predicate.edb("owns", 2),
    Predicate.edb("delegated", 2),
    Predicate.edb("blocked", 1),
    Predicate.idb("can_read", 2),
    Predicate.idb_query("has_any_document", 1),
    Predicate.idb_query("allow", 2),
]
policy = """
can_read(User, Doc) :-
    owns(User, Doc) or delegated(User, Doc),
    not(blocked(User)).
has_any_document(User) :- owns(User, _).
allow(User, Doc) :- user(User), can_read(User, Doc).
"""

with Engine() as engine:
    print(engine.limits.max_edb_facts)
    engine.register_domain("documents_next", predicates)
    ruleset = engine.load_inline_ruleset("documents_next", "documents.main", policy)
    edb = ruleset.edb()
    edb.add_fact("user", ["alice"])
    edb.add_facts([
        ("user", ["bob"]), ("user", ["mallory"]),
        ("owns", ["alice", "roadmap.pdf"]),
        ("delegated", ["bob", "roadmap.pdf"]),
        ("owns", ["mallory", "roadmap.pdf"]),
        ("blocked", ["mallory"]),
    ])
    result = ruleset.solve(edb)
    print(result.contains_fact("allow", ["alice", "roadmap.pdf"]))   # True
    print(result.contains_fact("allow", ["bob", "roadmap.pdf"]))     # True
    print(result.contains_fact("allow", ["mallory", "roadmap.pdf"])) # False
    print(result.derived_fact_count()) # 6: 2 can_read + 2 has_any_document + 2 allow
```

The declaration constructors match the unreleased C convenience initializers:

| C declaration | Python declaration | Flags |
| --- | --- | --- |
| `MAELYS_DATALOG_EDB(name, arity)` | `Predicate.edb(name, arity)` | `EDB` |
| `MAELYS_DATALOG_IDB(name, arity)` | `Predicate.idb(name, arity)` | `IDB` |
| `MAELYS_DATALOG_IDB_QUERY(name, arity)` | `Predicate.idb_query(name, arity)` | `IDB \| QUERY` |

They construct immutable `Predicate` values; they do not register or validate a
domain. `Engine.register_domain()` retains its existing validation and error
behavior. `QUERY` is an additional flag, not a third category of facts.
`Predicate(name, arity, flags)` and all `PRED_*` constants remain available for
other combinations, including `PRED_POLICY_FACT`. Unlike the C initializers,
Python constructors allocate ordinary Python objects.

One `ruleset.solve()` creates one opaque session and result. Several results can remain
open together because they do not share a session. `Engine.close()` closes
its results, sessions, input EDBs, and policy handles in that order.

## Native EDB additions, one complete solve

```text
add_fact(predicate, terms) ──┐
                            ├── owned opaque C input EDB
add_facts(iterable) ─────────┘          │
                                      ▼
                              ruleset.solve(edb)
                                      │
                         session_solve_edb(session, edb)
                                      │ one complete native solve
                                      ▼
                           opaque session → result
```

`add_fact()` calls the native append operation immediately. `add_facts()` accepts
an iterable of `(predicate, terms)` pairs, including a generator. It snapshots
terms into a temporary, entry-bounded staging area, then calls one atomic native
append. Neither method retains a Python fact list after returning: C owns copied
predicate names and UTF-8 symbol bytes. Later changes to the caller's term lists
cannot change the EDB. Integers and booleans are copied by value.

Staging uses this EDB's actual `fact_capacity`, not the profile maximum. A
capacity-four EDB rejects a five-item iterator before calling native code,
consuming at most the fifth item to detect overflow, never a sixth. For a batch
within that bound, native append checks remaining capacity atomically, including
facts already present. Python allocation remains bounded by the configured
capacity, not necessarily by the number of currently free slots.

Python type errors, malformed pairs, integers outside int64, embedded NULs,
failed iteration, native storage-limit failures and Python/CFFI allocation failures reject
the addition without appending any of that batch. Earlier successful additions
remain. Empty batches are allowed while the EDB is mutable. Iterator side effects
outside the batch are not rolled back. `len(edb)` counts stored entries before
deduplication. `edb.clear()` empties an unsolved EDB, including after a rejected
solve; `edb.close()` frees it and is idempotent.

| Operation | Checks / effect |
| --- | --- |
| `add_fact()` / `add_facts()` | Copy values; check term kinds, maximum arity, entry count and each UTF-8 string's byte bound. |
| `solve()` | Check the selected policy's domain, declared arity, symbol pool and per-predicate bounds; canonicalize and evaluate the whole input. |
| Successful solve | Result leases the session's reserved evaluation state. Python freezes that EDB for mutation until an explicit `reset()`. |
| `reset()` on an EDB | Start a new empty input batch in the same storage, including after a successful solve. Existing results are unaffected. |
| `close()` on an EDB | Free input storage; existing results remain valid. Parent `Ruleset`/`Engine` closure also frees EDBs. |

**Observable timing change:** the total entry limit (before deduplication) and
individual string bounds now fail at insertion, not at solve. Domain checks are
still deferred because an input EDB is not tied to one policy in a manifest.
This is full-batch evaluation, not incremental inference. Solving the same EDB
again obtains another result lease; a reusable session still permits only one live
result at a time. An input rejection publishes no result.

The C facade exposes `maelys_datalog_input_edb_create/add_fact/add_facts/count/clear/free`
and `maelys_datalog_session_solve_edb()`. Unlike Python's convenience lifecycle,
the C buffer is not frozen by solve: it may be cleared, changed or freed once
the call returns. C callers serialize access to each handle. The old array-based
`maelys_datalog_session_solve()` remains supported; old core EDB names and symbols
are unchanged. No private header is required.

### Fixed input-storage budgets

The native input buffer does not allocate while appending or clearing facts.
Its facts and copied strings share one fixed-capacity allocation made when
`ruleset.edb()` constructs it. Specify smaller budgets when known:

```python
edb = ruleset.edb(fact_capacity=64, text_capacity=8192)
edb.add_fact("owns", ["alice", "doc.pdf"])
```

`text_capacity` counts bytes for distinct predicate names and symbols, including
their terminating NULs. Repeated strings share storage, even across predicate
names and term values. Exceeding either budget raises an error without changing
the EDB. `clear()` empties an unsolved buffer; `reset()` also ends the mutation
freeze after a successful solve. Neither operation wipes old bytes. There is no
growth or fallback allocation.

The default entry budget is the loaded profile's maximum EDB fact count. The
default and maximum text budget is `engine.limits.input_edb_text_bytes`: 40 KiB
in the current SMALL and LARGE profiles (32 KiB of native symbol storage plus
8 KiB for predicate names), not a worst-case string allocation per fact. Smaller
explicit budgets are supported. Domain validation and deterministic result symbol
IDs are still assigned during solve; storage sharing does not expose native IDs.

For repeated requests, create the input buffer and a prepared session once:

```python
# Reuse the document policy from the example above.
edb = ruleset.edb(fact_capacity=64)
with ruleset.prepare() as session:
    for user, document in [("alice", "roadmap.pdf"), ("bob", "notes.pdf")]:
        edb.reset()
        edb.add_fact("user", [user])
        edb.add_fact("owns", [user, document])
        with session.solve(edb) as result:
            print(result.contains_fact("allow", [user, document]))
edb.close()  # Engine.close() also cleans up any remaining buffers.
```

```text
Initialize once: input arena + session scratch + result/provenance workspace
                               │
          reset → append → solve → query → close result
            ▲                                  │
            └────────── reuse storage ──────────┘
```

The reference backend performs no engine-owned allocation or free along that
native loop. A session permits only one live result: closing it releases the
lease, not the reserved workspace. Resetting the input cannot alter an existing
result, but does not release that lease either. `ruleset.solve(edb)` remains a
convenience that creates a new session per call; use `prepare()` for reuse.

Python itself and CFFI still allocate objects and temporary conversion arrays.
Policy/session initialization, custom backends and callbacks have separate
allocation behavior. Explanations use a Python-owned CFFI workspace and text
buffer, with no reference-engine heap allocation during preparation or writing.
This is **not** a
zero-malloc Python binding or a guarantee about allocations inside libc.
For native callers requiring a completely allocation-free input lifecycle,
`maelys_datalog_input_edb_storage_requirements()` plus `maelys_datalog_input_edb_init()`
accept aligned caller-owned memory. `create_with_capacity()` is the optional
one-allocation convenience path, and both use the same append implementation.

For example, after one valid buffered fact:

```python
edb.add_fact("owns", ["alice", "doc.pdf", "extra"])
result = ruleset.solve(edb)  # raises MaelysDatalogError
```

The native diagnostic is carried into `error.message`:

```text
Invalid fact at index 1: owns expects 2 arguments, received 3.
```

At solve, fact indices are **zero-based across the complete EDB**, not relative
to the last `add_facts()` call. Append diagnostics instead index the submitted
batch. Term indices are also zero-based. Unknown
predicates, invalid predicate kinds and capacity errors identify the relevant
cause. Symbol-capacity failures locate a contributing fact after canonical
symbol sorting; they do not necessarily identify the first offending fact in
input order. Diagnostics omit runtime symbol contents. `error.code` is the
native status and `error.hint` provides context; do not parse the message prose
as a stable machine-readable grammar.

## Limits and derived counts

`engine.limits` is an immutable `Limits` snapshot read from the loaded library
using `maelys_datalog_limit_get()`. It reports `max_symbols`,
`string_pool_bytes`, `max_predicates`, `max_rules`, `max_arity`,
`max_body_literals`, `max_depth`, `max_edb_facts`, `max_idb_facts`,
`max_facts_per_pred` and `max_string_bytes`. These are build capacities, not
current occupancy. The total input count limit applies **before deduplication**;
additional distinct-symbol, byte-pool and per-predicate bounds can reject a
smaller batch.

`result.derived_fact_count()` calls
`maelys_datalog_result_derived_fact_count()` without solving again. It counts
distinct derived IDB facts across all predicates, including non-queryable
intermediate predicates, and excludes runtime EDB and policy facts. Therefore
it need not equal the size of an enumeration of `allow`. Query permissions still
govern enumeration and membership checks. Calling it after closing the result
raises `RuntimeError`.

## Manifest loading and policy-local vocabulary

`engine.load_manifest(path, allow_test_only=False,
allow_undeclared_policy_atoms=False)` delegates to the native manifest loader:
source SHA verification, domain/query validation, and atomic rejection are not
reimplemented in Python. `path` accepts a string or `pathlib.Path`.
`ruleset.policy_count` reports the number of loaded policies. Select explicitly
with `ruleset.solve(edb, policy_index=1)` or `ruleset.prepare(policy_index=1)`;
indices are zero-based in the loaded set, not source-file indices.

The vocabulary opt-in is restricted to manifest loading. It admits ordinary
policy string constants into that policy's symbol table without changing the
registered domain. Inline loads remain closed. Distinct referenced policy atoms
are bounded by 256 entries and 63 UTF-8 bytes each (native pool limits may be
tighter). Standard filter patterns are not atoms: they are outside this count
and whitelist, with their separate 256-byte bound. The first filter argument,
if it is a literal constant, remains an ordinary policy atom.

## Prepared sessions, not incremental evaluation

```python
from maelys_datalog_next import Capability

# ruleset is already loaded; every iteration uses a complete input batch.
with ruleset.prepare(required_capabilities=Capability.EXPLAIN_FALSE) as session:
    for user in ("alice", "bob", "alice"):
        edb = ruleset.edb()
        edb.add_facts([("user", [user]), ("owns", [user, "doc.pdf"])])
        with session.solve(edb) as result:
            print(result.contains_fact("allow", [user, "doc.pdf"]))
```

`Session` reuses prepared native state, resetting runtime facts on every solve.
There is no delta insertion/deletion or reuse of previous derived facts. Close
the live result before another `session.solve()`; otherwise Python rejects the
call. A failed solve publishes no result and permits retry with a corrected
batch. `Session.close()` closes its live result first. `Ruleset.close()` and
`Engine.close()` cascade in result → session → policy order. Close is idempotent.
Ruleset, Session and SolveResult also support context managers.

Engine and all its handles are confined to the thread that created the Engine;
cross-thread use/close is rejected before entering C. Separate workers create
their own Engines; domain registration remains process-wide and serialized.
Use `with`/`close()` deterministically: GC finalizers only warn; they never free
native storage or release a result lease.

## Explanations and full diagnostics

`result.explain_true(predicate, terms)` and `result.explain_false(predicate,
terms)` return the native UTF-8 document unchanged. Their public signatures and
error behavior are unchanged, but they no longer call the legacy direct-text
function twice. Each call follows this sequence:

```text
live result
  → query workspace size/alignment
  → allocate aligned Python-owned storage
  → prepare ONE explanation (temporarily leases the result)
  → read cached text size → allocate text buffer → write text
  → copy UTF-8 text into a Python str
  → release explanation in finally → return independent str
```

The workspace and output buffer serve different purposes: the former retains
the bounded explanation, the latter receives its formatted text. Text size
excludes the terminating NUL; the binding reserves one extra byte. The workspace
owner stays alive through native release. If text sizing, allocation, writing or
decoding fails, `finally` still releases the handle, allowing the result to close
and its session to be reused. A failed preparation publishes no handle to release.

The reference backend does not allocate on this prepared native path. Python and
CFFI still allocate the workspace, conversion objects and returned string. There
is no cross-call explanation cache: two Python calls prepare two explanations.
Neither call reruns the solver. Native C callers can instead reuse their own
aligned arena and perform multiple writes from one prepared handle. Python keeps
that lower-level lifecycle internal, so existing callers need no extra `close()`.

Why-false retains its
`MAELYS-DATALOG-WHY-FALSE-v1` status: `complete`, `truncated`, or `not-applicable`.
Truncated output is not proof of non-derivability. The reference bounds are 128
candidate rules, 4,096 substitutions per rule, depth 10 and 16 diagnostics;
filter work is bounded too. Limits are not caller-tunable. Text may contain
sensitive policy and input values; do not log it indiscriminately.

Unknown query symbols raise native `NOT_FOUND` for explanations (unlike the
membership query, which returns False); unsupported capabilities raise native
`UNSUPPORTED`. Errors must not be converted into a policy denial or fabricated
explanation. Standard `starts_with`, `ends_with`, `contains` are available in
policy source and participate in Why-true/Why-false. They are ground-only,
non-generative filters, not functions that bind previously unbound variables.

`MaelysDatalogError.status` and the retained alias `.code` carry the operation's
native status. `.diagnostic` is an immutable `Diagnostic` containing `source`,
`code`, `phase`, `line`, `column`, `message`, `hint`. Its code is **not** the
operation status. APIs without a diagnostic output leave that object empty.
The convenient `.message` and `.hint` remain available. Do not parse prose.

## Identities, capabilities and work limits

- `ruleset.fingerprint`: identity of the loaded policy set.
- `session.fingerprint` / `result.fingerprint`: selected policy authority.
- `session.execution_fingerprint` / `result.execution_fingerprint`: additionally
  binds the backend identity, required capabilities, effective work budget and
  native size profile. It does not hash runtime facts or sign an attestation.

`prepare()` and convenience `solve()` accept `required_capabilities` (a
`Capability` bitmask) and `work_limit` (uint64; zero selects the native default
1,048,576 host work units). The backend is explicitly the built-in reference.
**That backend does not advertise WORK_LIMIT:** a nonzero `work_limit` or an
explicit requirement for WORK_LIMIT raises UNSUPPORTED at preparation. The
default must not be presented as an enforced solver deadline. Work accounting
in the extension SDK is cooperative, not a wall-clock deadline or sandbox.
Native rejection never silently selects another implementation.

## Raw result views

`result.enumerate_raw(predicate, arity)` returns tuples of immutable `ResultTerm`
objects. `kind` is `symbol`, `integer` or `boolean`; `value` is respectively a
result-local symbol ID, int64 or bool. Call `term.resolve()` or
`result.resolve_term(term)` while the owning result is open. Resolving against
another result or after closure fails. These views do not keep the native result
open after explicit closure. `enumerate_predicate_facts()` instead copies Python
values that remain usable after closure. Both enumerate derived IDB facts only
and preserve query permissions.

## Remaining boundaries

This remains experimental, not a drop-in replacement for `maelys_datalog`:

- Garbage collection **never releases native resources**. `SolveResult`,
  `Session` and `Edb` emit `ResourceWarning` if collected unclosed; their
  destructors make no native calls. Enable these normally hidden warnings with
  `python -W always::ResourceWarning`. Owners retain their children, so merely
  deleting a result variable does not necessarily collect it or warn immediately.
  Close the result to release its session lease; close the owning Engine/Ruleset
  or use context managers to free the graph deterministically. An abandoned
  graph can retain native memory until process exit, even after the warning.
- Inputs are strings/int64/bools, never raw result IDs; no ruleset `intern_symbol`.
- EDB additions copy into native opaque storage; policy-specific validation occurs at solve.
- Native custom frontend/backend/planner/filter callbacks and program IR builders
  are separate extension-SDK APIs, not Python callback registrations here.
- True incremental inference is not provided by the current engine API.

The old binding and its documentation remain separate and unchanged.
