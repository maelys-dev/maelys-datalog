# Application type migration for 0.10.0

Application and extension interfaces share a single declaration for each kind
of input data. This is a coordinated source migration; obsolete type names are
removed, without compatibility typedefs. Rebuild C/C++ and CFFI consumers.

| Before | Application declaration |
| --- | --- |
| `maelys_datalog_public_value_t`, `maelys_datalog_input_term_t` | `maelys_datalog_value_t` |
| `maelys_datalog_public_fact_t`, `maelys_datalog_input_fact_t` | `maelys_datalog_fact_t` |
| `maelys_datalog_public_predicate_t` | `maelys_datalog_predicate_t` |
| `maelys_datalog_public_domain_t` | `maelys_datalog_domain_t` |
| `maelys_datalog_public_term_view_t` | `maelys_datalog_term_view_t` |
| `maelys_datalog_public_fact_view_t` | `maelys_datalog_fact_view_t` |
| `maelys_datalog_public_diagnostic_t` | `maelys_datalog_diagnostic_t` (new protocol) |

An input value uses `MAELYS_DATALOG_VALUE_SYMBOL`, `VALUE_INTEGER` or
`VALUE_BOOLEAN` (each prefixed with `MAELYS_DATALOG_`). It cannot be a variable.
Symbols are text on input, whereas result views carry result-scoped symbol IDs.
The types are distinct because their identity and lifetime contracts differ.

Native solver facts, terms, EDBs, rulesets, policy sets, prepared sessions and
solve results now have `maelys_datalog_internal_*_t` names. In particular, the
old native `maelys_datalog_fact_t` is `maelys_datalog_internal_fact_t`; do not
reinterpret an old native fact as a new application fact. Materialization
validates and interns application values explicitly. Its input strings are
borrowed only for the call, and boolean normalization does not mutate the input.

Use `maelys_datalog_limit_get` instead of `maelys_datalog_get_build_limits` and
`maelys_datalog_build_limits_t`. These are build capacities, not occupancy or
future per-session quotas. Unknown limit IDs return `UNSUPPORTED` without
changing the output. The Python build-limit object and Wasm packed-limit result
retain their language-level shape while reading the same scalar C accessor.

The common data rename does not change the layout of the application values,
facts or views. The input copy formerly made before materialization is removed;
canonical output storage remains reserved for external backends. No reduction
in session reservation or performance improvement is promised by the rename.
Existing result/explanation leases, admission checks and allocation guarantees
continue to apply. A diagnostic/callback contract change is a separate ABI
change; rebuilding against shared names alone is not proof of compatibility
with such a change.


## Advanced operations on the same objects

Include `maelys/datalog_advanced.h` for in-memory manifest bundles, caller-owned
policy storage, domain builders, composed session configuration, structured
explanations, filter statistics and the deny/reduce/allow decision helper. These
operations take the same policy/session/result/prepared-explanation handles.
The shared semantic enums and statistics record live in `datalog_details.h`;
there is no second simple/advanced diagnostic or fact declaration.

A bundle has one `policy_bundle_entry_t` per source, matched by policy ID rather
than array position. The loader validates hashes, permissions and query
whitelists as the file loader does. The convenience path allocates one fixed
policy object; the `_in` variants place it in aligned caller storage, whose
requirements are queried for the loaded profile. It currently reserves space
for eight policies, even for inline source. Initial loading may overwrite this
unused storage on failure; it does not replace an existing live policy. Close
before reuse. JSON parsing and extension code may allocate independently.
This is not a claim of a zero-heap compiler or a new memory profile.

The domain builder replaces access to a mutable native registry. Its callback
runs on a loading candidate, with sticky failures; a rejected candidate is
never published. Atoms are installed after a successful callback. Registration
copies text and compares repeated declarations, including callback identity and
metadata; incompatible declarations fail. Domains remain process-global.

A session configuration can select either a copied backend descriptor or a
sealed context plus backend name, together with capabilities, work limits and
explanation workspace options. Setting one selection replaces the other. The
context is retained by the configuration. NULL name selects its reference
backend. Unsupported combinations, such as a reference-only workspace with a
custom backend, fail explicitly. Callbacks remain borrowed executable code.
No resource quotas, delta transport, SMALL incremental or elastic mode is added.

Structured records have fixed layouts for consumer API 2; later incompatible
outputs need a new accessor or API contract, not unchecked appending.
Structured explanation accessors read the already prepared workspace and hold
the same result lease as text output. They never rerun proof extraction. Output
records are copies; their text pointers borrow the live result. Do not place
outputs inside explanation storage. Step, rule, variable and filter indices are
local to this program/explanation, never persistent streaming identities.
Read only the fields selected by premise/obstacle kind: facts use `atom`,
comparisons use `lhs/rhs/op`, filters use `filter_value` and their program/kind,
and aggregates use source `atom`, projection and value. Aggregate mismatch
obstacles include computed `lhs` and expected `rhs`; empty extrema have no such
values. Why-false patterns retain their unbound mask and IR variables. Supports
and bound substitutions are exported separately. Truncation and search limits
are observations, not proof that no other explanation exists.

Structured access and filter statistics currently support the canonical
reference backend. Other backends return `UNSUPPORTED`, even when they support
text explanations. This explicit limit avoids interpreting foreign storage as
a native graph. A custom descriptor copied from reference is still custom.
The existing text capability bits do not promise this new representation.

## Diagnostic and callback migration

The consumer API is **version 2**, frontend/program ABI **2**, backend ABI **4**.
Rebuild every consumer. Old callback descriptors are rejected before invocation.
Extension-package and filter/planner descriptor layouts retain their independent
version 1; nested frontend/backend versions are checked on registration.
ABI 4 here names the diagnostic protocol, not the future incremental resource
contract. A later incompatible delta contract needs a distinct ABI number.

Initialize each diagnostic with `MAELYS_DATALOG_DIAGNOSTIC_INIT`, or call
`diagnostic_init(storage, bytes)`. `{0}` alone is no longer valid. Clear preserves
the declared storage capacity and version, resets all scalar fields, and empties
strings by their first NUL only. String tails are unspecified; do not serialize
whole diagnostic objects or treat clear as secure erasure. Initialization zeros
the known object once. The successful `solve_edb` path delegates to `solve` and
performs one reset, not two. Version 1 requires the entire known
object and its natural alignment. Smaller storage is refused with
`STORAGE_TOO_SMALL`, unknown versions with `UNSUPPORTED`, before payload writes
or callback invocation. Larger same-version storage is accepted, but bytes
beyond the known object remain untouched. This is an explicit v1 protocol, not
an assertion that arbitrary future append operations are ABI-compatible.
Unknown presence bits must be ignored by readers; unknown diagnostic codes must
not be inferred from text. Future compatible evolution must preserve known
field offsets, alignment and meanings. Otherwise it requires a new contract.

`status` carries the status associated with a populated diagnostic; `code`
carries the precise cause and has one typed namespace. `source == NONE` means
no diagnostic was supplied: always use the function's return status. Generic
operation rejections use `DIAG_OPERATION_REJECTED`, not a negative status in
`code`. Independent presence bits describe location, predicate, capacity,
depth, comparison, arity, rule and source context. `limit_kind` identifies a
known public budget; zero means the producer cannot identify it. IDB exhaustion
records the violated bound at the failed insertion: relation-local count and
limit for a per-predicate overflow, global count and limit for total overflow,
with the known predicate in its own section. Counts include the attempted
insertion. The compact record stores the bound identity explicitly rather than
inferring it from capacities that might coincide. Unset sections are zero and
must not be interpreted. Predicate names are exported from the current
vocabulary; unavailable native predicate/rule IDs produce no presence bit.
Comparison kind/operator integers use the corresponding IR values; zero means
unavailable. Diagnostic text is owned, bounded and NUL-terminated; source paths,
messages and hints can be truncated by their producer. Typed fields do not
require parsing those strings.

The reference solver retains its 32-byte private working diagnostic. On the
measured macOS arm64 ABI, the caller diagnostic grows from 568 to 1328 bytes,
the optional configuration from 40 to 408 bytes, and the policy handle gains
8 bytes in its tail. The session, native result, ruleset, prepared input,
EDB and fact sizes and recorded hot-member offsets remain unchanged in both
profiles. These are layout observations, not absolute cache alignment or speed
claims. The private-to-public conversion happens at the boundary, outside the
solver's recursive frames.

The coordinated A3/A4 follow-up replaces native-object Python/Wasm bindings
with public-SDK consumers and closes the distributed header surface. In 0.10.0,
native archives and CMake installations use one public install inventory.
Include `maelys/datalog.h` for application operations, `maelys/datalog_advanced.h`
for advanced operations on those handles, or the relevant `maelys/` extension
header. The historical aggregator, version-macro header and private implementation
headers are no longer shipped. Native implementation types are not replacements
for the public values, facts and opaque handles. External consumers are built
and run against both the installed prefix and an extracted archive before
publication; C11/C++17 checks reject historical/private includes.
