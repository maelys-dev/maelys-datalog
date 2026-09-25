# Maelys Datalog Python binding

Backend ABI 5 preparation/acceptance ships in 0.11.0 (unreleased). This binding
exposes no backend descriptors; its API and implementation are unchanged.

`maelys_datalog` is the single Python binding. From the 0.10.0 migration onward,
it uses the implementation developed as `python-next`, directly through public
`<maelys/datalog.h>` and `libmaelys_datalog_shared`. The old native-object shim
and the `maelys_datalog_next` import are removed, with no compatibility layer.
See [migration from V1](#migration-from-v1-0100) before updating an application.

The binding requires consumer API 2. Backend ABI numbers, native structures and
extension descriptors are not part of its Python interface. The reference
backend is selected by the native session API. Python/CFFI conversions and
objects allocate; this is not a zero-malloc Python binding.

## Build

Python 3.10+, CFFI, setuptools, a C11 compiler, and CMake 3.16+ are required.
Build and install one native SDK, then compile the binding against that prefix:

```sh
python3 -m venv build/python-venv
build/python-venv/bin/python -m pip install cffi setuptools pytest
cmake -S . -B build/cmake-small
cmake --build build/cmake-small --parallel 4
cmake --install build/cmake-small --prefix "$PWD/build/sdk-small"
build/python-venv/bin/python bindings/python/build_cffi.py \
  --sdk-prefix "$PWD/build/sdk-small"
MAELYS_DATALOG_SDK_PREFIX="$PWD/build/sdk-small" \
MAELYS_DATALOG_EXPECT_PROFILE=small PYTHONPATH=bindings/python \
  build/python-venv/bin/python -m pytest -q tests/python
```

For LARGE, use separate CMake and installation directories, configured with
`-DMAELYS_DATALOG_PROFILE_LARGE=ON`, and set `MAELYS_DATALOG_EXPECT_PROFILE=large`.
Rebuild CFFI and start a fresh interpreter whenever changing the SDK/profile.
Never mix libraries from different revisions or profiles in one interpreter.
The SDK prefix must contain matching headers and library from one installation;
`--engine-dir`, `--build-dir`, the CMake shim option and shim target are removed.

`build_cffi.py` copies the installed shared library next to the Python module,
using a fresh inode, and writes the extension into
`bindings/python/maelys_datalog/`. Temporary C compilation files are under
`bindings/python/build/`. This is a native source build, not a wheel publisher
or a new package release mechanism.

CI uses `tools/check_python_binding.sh BUILD_DIR PYTHON small|large`. It installs
into a fresh prefix, copies the Python package and tests outside the repository,
clears ambient C include/library search paths, and compiles and tests there.
There is no checkout fallback or optional parity skip. Tests verify the loaded
profile, known expected facts/text, ownership, errors, input atomicity, prepared
sessions and explanations on SMALL/LARGE. Negative compilation controls reject
API 1 and 3 while retaining the same layouts, so they exercise the version guard
itself. The domain registry is process-wide
and bounded, so tests and applications reuse identical declarations.

## Migration from V1 (0.10.0)

The import remains `maelys_datalog`. Applications using the experiment change
`from maelys_datalog_next ...` to `from maelys_datalog ...`. Update all callers in
one step; neither old implementation nor forwarding import is retained.

| Previous interface or behavior | Single binding |
| --- | --- |
| `Predicate(..., kind_flags=...)`, `.kind_flags` | `Predicate(..., flags=...)`, `.flags`, or the named declaration constructors below |
| `BuildLimits` | Immutable `Limits`, read from `engine.limits` |
| `Term.symbol_id`, `ruleset.intern_symbol`, `ruleset.symbol_text` | Pass `str`, `int`, `bool` values; symbols are resolved by the native session |
| `Term.integer(n)`, `Term.boolean(b)` | Pass `n`, `b` directly |
| `InputTerm`, `ResolvedTerm`, `Fact`, `RawFact`, native `TERM_*` constants | Use `str \| int \| bool`, tuples of those values, or `ResultTerm` for raw result views |
| `enumerate_predicate_facts_raw()` | `enumerate_raw()`; each `ResultTerm` belongs to one live result, and is never an input term |
| `explain_fact_text()` | `explain_true()`; add `explain_false()` for bounded Why-false |
| Absent Why-true returns `None` | Known vocabulary but no derived fact returns canonical text with `status=not-derived`; an unknown symbol raises `Status.NOT_FOUND` |
| `C.ERR_*`, `DomainAlreadyRegisteredError`, `DomainRegistryFullError` | Catch `MaelysDatalogError`, compare `.status` with `Status`; conflicting domains are `INVALID_FIELD`, a full registry is `PAYLOAD_TOO_LARGE` |
| Domain registry discovery through the shim | Register identical declarations again; registration compares and copies through the native public API |
| Shared ruleset symbol lock | Each Engine and all its handles stay on their creating thread; create a separate Engine inside each worker |
| Destructors free native handles | Explicit `close()` or context managers; garbage collection only warns and does not free native resources |
| EDB mutation permanently frozen after solve | Frozen until explicit `reset()`; repeated solves remain valid and existing results are independent |

Policy-specific input errors (undeclared predicate, wrong declared arity, symbol
budget) are reported at solve. Append still checks representation and its own
fact/text budgets atomically. A prepared session allows only one live result;
close it before the next solve. Convenience `ruleset.solve(edb)` creates a
separate session for each result and therefore allows multiple live results.
Neither path performs incremental evaluation.

Ground queries still cover query-capable policy, EDB and IDB facts; enumeration
covers derived IDB facts only. An unknown query string returns `False` without
interning, while invalid predicates remain errors. Explanations preserve native
text, including truncation. Resolve or copy result terms before closing their
result; copying a numeric symbol ID into another result has no portable meaning.

```python
from maelys_datalog import MaelysDatalogError, Status

try:
    engine.register_domain("documents", predicates)
except MaelysDatalogError as error:
    if error.status == Status.INVALID_FIELD:
        raise RuntimeError("conflicting domain declaration") from error
    raise
```

## Example

```python
from maelys_datalog import Engine, Predicate

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
    engine.register_domain("documents", predicates)
    ruleset = engine.load_inline_ruleset("documents", "documents.main", policy)
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

The declaration constructors match the C convenience initializers added in 0.4.0:

| C declaration | Python declaration | Flags |
| --- | --- | --- |
| `MAELYS_DATALOG_EDB(name, arity)` | `Predicate.edb(name, arity)` | `EDB` |
| `MAELYS_DATALOG_EDB_QUERY(name, arity)` | `Predicate.edb_query(name, arity)` | `EDB \| QUERY` |
| `MAELYS_DATALOG_IDB(name, arity)` | `Predicate.idb(name, arity)` | `IDB` |
| `MAELYS_DATALOG_IDB_QUERY(name, arity)` | `Predicate.idb_query(name, arity)` | `IDB \| QUERY` |
| `MAELYS_DATALOG_POLICY_FACT(name, arity)` | `Predicate.policy_fact(name, arity)` | `POLICY_FACT` |
| `MAELYS_DATALOG_POLICY_FACT_QUERY(name, arity)` | `Predicate.policy_fact_query(name, arity)` | `POLICY_FACT \| QUERY` |

They construct immutable `Predicate` values; they do not register or validate a
domain. `Engine.register_domain()` retains its existing validation and error
behavior. `QUERY` is an additional permission, not an origin of facts; a
query-only declaration is rejected when a policy loads its predicate registry
(registration itself stores the flags). EDB facts come from request inputs, IDB
facts from rule evaluation, and POLICY_FACT facts from trusted policy source.
The `_query` suffix does not change that origin or authorize input injection.
`Predicate(name, arity, flags)` and all `PRED_*` constants remain available for
explicit declarations of the same six combinations. Unlike the C initializers,
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
allocation behavior. By default, explanations use a Python-owned CFFI workspace
and text buffer, with no reference-engine heap allocation during preparation or
writing. The optional `prepare(explanations=...)` mode reserves that workspace
once in the native session instead; Python still allocates the text buffer.
This is **not** a
zero-malloc Python binding or a guarantee about allocations inside libc.
For native callers requiring a completely allocation-free input lifecycle,
`maelys_datalog_input_edb_storage_requirements()` plus `maelys_datalog_input_edb_init()`
accept aligned caller-owned memory. `create_with_capacity()` is the optional
one-allocation convenience path, and both use the same append implementation.

The CFFI utility `maelys_datalog_input_edb_view()` borrows the ordered raw input
entries, including duplicates. Its array and strings are read-only and must not
be used after a successful mutation or close of the EDB. It is not a Python
iterator or an owning copy. The native last-N window API is in a separate header
and is not yet exposed as a Python wrapper.
The CFFI utility `maelys_datalog_input_edb_text_usage()` reports interned
predicate/symbol bytes including NUL terminators and the configured text capacity.
Repeated strings share storage; rejected appends preserve usage and clear resets
it to zero. This measures input text only, not native session or Python memory.

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

The C facade names the three values together:

| C value | Python spelling | Permission |
| --- | --- | --- |
| `MAELYS_DATALOG_PUBLIC_ALLOW_NONE` (`0u`, permanently) | `engine.load_manifest(path)` | Neither optional permission |
| `MAELYS_DATALOG_PUBLIC_ALLOW_TEST_ONLY` | `allow_test_only=True` | Admit enabled `test_only` policies |
| `MAELYS_DATALOG_PUBLIC_ALLOW_UNDECLARED_POLICY_ATOMS` | `allow_undeclared_policy_atoms=True` | Admit policy-local undeclared constants |

The two bits are independent and combine with `|`; they are not ordered
strict/permissive modes. `ALLOW_NONE` is not a denial: `ALLOW_NONE | X` is `X`.
Python keeps the two boolean keywords, both defaulting to `False`, with no
numeric mask or `allow_none` option. Without the test-only permission, an enabled
`"mode": "test_only"` entry fails the whole load with `FORBIDDEN`, rather than
being skipped. Once admitted, it is evaluated normally; the engine neither
simulates evaluation nor detects a production environment. Disabled entries keep
the manifest's existing behavior. `allow_test_only` bypasses neither SHA-256
verification nor atom checks. Predicate, capability and capacity checks remain
active with either or both permissions. The public C facade rejects every
unknown flag bit with `INVALID_ARGUMENT`, also when combined with known bits,
and clears `out_policy` on failure. Older libraries must reject unknown future
bits rather than ignore them.

`atoms` belongs to the registered domain, not the manifest. It authorizes
symbolic constants in Datalog predicate arguments and creates no facts. An EDB
`blocked/1` can receive `edb.add_fact("blocked", ["mallory"])` without declaring
`"mallory"`, while a rule checks `not(blocked(User))`. Writing
`blocked("mallory").` in the source requires `POLICY_FACT` origin and an allowed
constant (unless the manifest explicitly permits a local constant). Writing
`blocked("mallory")` in a rule body requires the constant too, even when
`blocked/1` is EDB: the distinction is source text versus runtime input.

`Engine.load_inline_ruleset` uses `maelys_datalog_policy_load_inline()`, which
has no flags, checks Datalog domain constants without an override, and reads no
manifest `test_only` metadata. Some legacy/advanced inline C functions have a
reserved argument that must be zero; they are different signatures, not an
additional permission path in this facade.

The vocabulary opt-in is restricted to manifest loading. It admits ordinary
policy string constants into that policy's symbol table without changing the
registered domain. Inline loads remain closed. Distinct referenced policy atoms
are bounded by 256 entries and 63 UTF-8 bytes each (native pool limits may be
tighter). Standard filter patterns are not atoms: they are outside this count
and whitelist, with their separate 256-byte bound. The first filter argument,
if it is a literal constant, remains an ordinary policy atom.

## Prepared sessions, not incremental evaluation

```python
from maelys_datalog import Capability

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

### Optional reusable workspace (0.5.0)

```python
from maelys_datalog import ExplanationKind

with ruleset.prepare(explanations=ExplanationKind.TRUE | ExplanationKind.FALSE) as session:
    with session.solve(edb) as result:
        print(result.explain_true("allow", ["alice", "roadmap.pdf"]))
        print(result.explain_false("allow", ["mallory", "roadmap.pdf"]))
```

No workspace is reserved by default. This option reserves one native allocation
at session creation, bounded by the maximum of the selected kinds' storage
bounds. Each `explain_*` then measures and writes through the native session
cache: one preparation, no new CFFI exploration arena and no reference-engine
allocator calls on either call. Python/CFFI still allocate argument conversions,
output buffers and strings. The old default already prepared once without native
engine allocation; the improvement is reusing its workspace, not removing a
second exploration from that old Python path.

The one-entry cache survives matching repeated calls and ordinary membership
queries; changing kind/predicate/typed values replaces it. A result close clears
it automatically, including after a text-allocation/write/decode exception.
Returned strings remain independent. Unselected kinds raise native `UNSUPPORTED`,
not an allocating fallback. Session confinement and explicit context management
are unchanged. `ruleset.solve(edb, explanations=...)` forwards the same option,
but only an explicitly reused session amortizes its creation across requests.

### Default prepared path

`result.explain_true(predicate, terms)` and `result.explain_false(predicate,
terms)` return the native UTF-8 document unchanged. Their public signatures
are unchanged. Without the opt-in, they do not call the legacy
direct-text function twice. Each call follows this sequence:

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
is no cross-call explanation cache on this default path: two Python calls prepare two explanations.
Neither call reruns the solver. Native C callers can instead reuse their own
aligned arena and perform multiple writes from one prepared handle. Python keeps
that lower-level lifecycle internal, so existing callers need no extra `close()`.

Both documents start with `MAELYS-DATALOG-v2`, followed by `document=why-true`
or `document=why-false`. Dispatch readers on the second line; the first line
alone no longer means Why-true. Why-false keeps its statuses: `complete`,
`truncated`, or `not-applicable`. This replaces its historical
`MAELYS-DATALOG-WHY-FALSE-v1` header; there is no compatibility-output switch.
Python returns the native text and does not parse the old header. Why-true
output is unchanged and may also be truncated.
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
`code`, `phase`, `line`, `column`, `message`, `hint`, and the independent
sections selected by `present` (predicate, capacity, depth and other details).
Its code is **not** the operation status. `Status` exposes all named native
operation statuses for comparisons. APIs without a diagnostic output leave that object empty.
The convenient `.message` and `.hint` remain available. Do not parse prose.

## Stratified count (0.6.0)

The reference backend supports `count(I, event(I,G,_), N)` in rule bodies,
with `G` bound by an ordinary positive atom. It counts distinct typed `I`
values and emits zero for an explicitly bound empty group. The usual typed
result enumeration and `explain_true` / `explain_false` methods work with counts.
`Capability.AGGREGATES` requests this contract explicitly; loading an aggregate
program already makes it required. This capability currently means stratified
count only. Unsupported backends fail before preparation.
Each `Session.solve()` still recomputes its input snapshot; it is not
a streaming update API. Python conversions and output strings still allocate.

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

The following contracts apply to the single binding:

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

## Numeric aggregates (0.7.0)

`min(V,event(_,G,V),N)`, `max(...)` and `sum(...)` use the same syntax and
scope as count. The separate capabilities are `Capability.MIN`, `.MAX`, `.SUM`;
`.AGGREGATES` still means count only. Numeric projections must be integers in
0..2147483647. Empty extrema produce no tuple; empty sums produce zero. Sum
adds once per distinct complete source fact, so different event IDs with equal
values each contribute. A matching non-integer or overflow rejects the solve
with `INVALID_FIELD`. These are snapshot operators; see the
[aggregate contract](../../docs/specifications/maelys-datalog-v2/aggregates.md).
