# Transactional window context

The 0.13.0 batch groups the unified `<maelys/datalog_window.h>` migration with
additive operations on the existing two window handles. No `program_info`,
consumer/program/backend ABI version, existing signature or public record layout
changes. The retired include path is the deliberate source migration.

## Mutable static EDB

Use `window_storage_requirements_configured` / `window_init_configured`, or
the corresponding `group_window_*` functions, to reserve a raw static-fact
capacity in `maelys_datalog_window_options_t` in addition to the existing
event/contribution capacity. Set `struct_size` to `sizeof` that record and
reserved `flags` to zero; NULL options mean defaults. Legacy
constructors mean zero static capacity. Static input starts empty. All adapter
memory is in the queried caller arena; no operation allocates or grows storage.
The query covers both banks and checked alignment/size arithmetic, but excludes
the borrowed sessions and their result/explanation storage.

Each input bank stores a static prefix followed by ordered events/contributions,
sharing its interned text budget. The sum of reserved raw static and event slots
must fit `MAX_EDB_FACTS`, even if duplicates would make a small union. Events
retain their independent capacity and FIFO admission; adding static storage
does not silently shrink it. The group adapter's `unique_facts` limits the
**combined** union and may be up to the sum when using the new constructors.
Both sort arrays also reserve that sum. Session/program/per-predicate limits
still apply; a storage plan does not guarantee that every snapshot can solve.

`window_replace_static(window, facts, count, diagnostic)` replaces the entire
static input and immediately recomputes it with the retained events. The group
function has the same contract. NULL/0 clears it. This neither inserts an event
nor consumes an occurrence/group ID, including after that cursor is exhausted.
An authorization revocation therefore takes effect without waiting for a future
event. It can change positive, negative and aggregate conclusions.

Static facts are complete typed facts, with no implicit ID term. They use the
same domain validation, boolean normalization and string interning as normal
inputs. Raw duplicates consume static slots but have set semantics at solve.
There is no special namespace: equal static/event/policy facts are equal under
the existing runtime rules. A group-union fact survives while static input or
any retained group supplies it. Static facts do not expire on FIFO eviction.

The adapter builds the complete candidate in its other bank/session. Only after
solving succeeds and the old result lease can be released are static input,
result and views published together. All failures preserve the committed bank,
metadata and cursor, including text/capacity/aggregate/backend failures and a
prepared explanation holding the result. Release that explanation and retry.
Candidate storage may change, and external callback side effects are outside
the rollback guarantee. Backend ABI 5 sees canonical complete snapshots; commit
runs only on acceptance, and a discarded candidate is aborted through the
existing result lifecycle. No backend delta API or new capability is implied.

`*_static_facts` returns the ordered raw static slice, including duplicates.
`window_events`, group descriptors and group contributions remain event-only.
Group `facts` and `usage.unique_facts` include static input; text usage covers
both populations. All views are borrowed, read-only and invalidated on any
successful mutation/free, including a replacement with equal content. Failed
operations preserve them. Never pass a borrowed view back into a mutating
operation: input, output and diagnostic ranges must remain disjoint from the
adapter arena. Access is serialized, and reentry is rejected.

## Validation and release boundary

Independent snapshot tests cover both adapters, authorization removal without
an insertion, late lease rejection with readable premises, canonical IDs,
normalization, caller text reuse, duplicate suppliers, FIFO retention, overflow,
short storage, arithmetic overflow and closed handles. The installed static and
shared SDK run the same consumer. White-box allocation guards compare complete
committed banks/metadata on rejection with engine allocators disabled. The
external recording provider checks exact canonical input and commit/abort
behavior on replacement, callback failure and clearing without an insertion.
These checks make no timing or whole-application memory claim.

Before cutting 0.13.0, build the downstream consumer against a clean installed
integrated candidate SDK and record revisions, profile, commands, outcomes and
required migrations. Include review alone is not that integration replay.
Downstream requirements arrive as written proposals; their implementation code
is not imported into this repository.
