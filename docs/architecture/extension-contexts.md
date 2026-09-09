# Extension declarations and isolated contexts

The four extension points share discovery, identity, registration, selection
and testing. They intentionally keep distinct typed callbacks: lowering syntax,
executing a program, selecting joins and evaluating filters are not the same job.

Include `maelys/datalog_extension.h`. It adds envelope ABI v1 without changing
program ABI, backend ABI v2 or module ABI. Existing public entrypoints remain
available. Internal/legacy ruleset POD consumers must rebuild; use opaque handles
for new integrations.

## One declaration, four typed components

`maelys_datalog_extension_t` describes one package with a name, semantic ID and
arrays of frontend, backend, planner and filter descriptors. Empty arrays are
allowed, but a package must contain at least one component. No constructor,
generic untyped callback or implicit global registration is involved.

Registration validates and copies all descriptors and identity strings
transactionally. A bad component or collision leaves the catalogue unchanged.
Same-kind names and semantic IDs are unique, as are package identities. Built-in
names cannot be replaced. Registration order is not a selection mechanism.
Code pointers are borrowed: keep the provider code loaded until its last policy,
session and context are released. Context ownership is not library ownership.

Names are lowercase identifiers beginning with a letter, up to 63 bytes.
Semantic IDs allow letters, digits, underscore, dot and hyphen, up to 127 bytes.
Change a component semantic ID when its observable meaning changes.
Package identity describes distribution, not execution: unused components do
not enter a program fingerprint merely because they were registered.

## Create, register, seal, use

```c
maelys_datalog_context_t *context = NULL;
maelys_datalog_extension_t extension = example_filter_extension();
/* Check every returned status; error cleanup omitted from this overview. */
maelys_datalog_context_create(&context);
maelys_datalog_context_register(context, &extension);
maelys_datalog_context_seal(context, NULL); /* default join heuristic */

/* Register the application's domain before loading. NULL = standard Datalog. */
maelys_datalog_context_load_inline(context, NULL, "my_domain", "policy",
                                  source, source_length, &policy, &diagnostic);
maelys_datalog_context_session_create(context, policy, 0, NULL, 0, 0, &session);
maelys_datalog_context_free(context);
/* policy and session retain the catalogue; release both normally when done. */
```

Use the standalone examples for compilable code and checked errors.

Build and register on one thread. Seal once, then publish with normal application
thread synchronization. A sealed catalogue is immutable and may serve independent
sessions on different threads. This does not make a single session concurrently
mutable or make a provider's own global state thread-safe.

Policies retain their catalogue; prepared sessions retain it independently.
Freeing the caller's context or policy handle does not invalidate a live session.
There is no thread-local or process-global “current context”.

| Component | Selection |
| --- | --- |
| Frontend | Name at `context_load_inline`; NULL means built-in Datalog |
| Backend | Name at `context_session_create`; NULL means reference |
| Planner | Name once at `context_seal`; NULL keeps the reference heuristic |
| Filter | Name in the source/IR, resolved only in that policy's catalogue |

A planner chooses only among host-provided safe candidates. An independent
backend owns its own planning; selecting the host planner does not inject it into
every backend. The reference backend consults the selected host planner.
The [bundle example](../../sdk/examples/bundle/README.md) shows two cooperating
components: its frontend lowers `permit "alice"` to IR referencing the filter
declared in the same package. The frontend pins that filter's semantic ID;
missing or incompatible providers reject loading instead of changing behavior.
An unknown selection returns `NOT_FOUND`, never silently substitutes a provider.
A policy from a different context is rejected by `context_session_create`.
Inspect component names and semantic IDs through `context_component_count/info`;
the API exposes metadata, not mutable callback tables.

## Errors and limits

- `INVALID_ARGUMENT`: malformed envelope/descriptor, ABI/size mismatch or null argument.
- `INVALID_FIELD`: duplicate name/semantic ID or policy/context mismatch.
- `INVALID_STATE`: loading before seal, registering or sealing after seal.
- `NOT_FOUND`: absent named component or out-of-range catalogue index.
- `PAYLOAD_TOO_LARGE`: bounded catalogue capacity exceeded.
- `UNSUPPORTED`: selected backend lacks a required program/caller capability.
- `INTERNAL`: allocation failure; no partially registered package is published.

The catalogue admits 16 packages, 16 frontends and 16 backends including the
built-in instance of each, 16 planners, and 16 external filters plus three
standard filters. Metadata strings are borrowed until context destruction;
do not mutate them. Registration allocates a bounded temporary catalogue;
sealed lookups do not allocate or mutate it.

## Compatibility migration

The old process-global filter/planner registry remains the default for old
policy loaders. Its startup-only registration and first-load sealing behavior
are unchanged. Existing direct frontend/backend selection APIs also remain.

Migrate an application by replacing global registration with a declaration,
creating/sealing a context, then switching inline loading and session selection
to the context APIs. Existing query, explanation, policy and session APIs work
on those handles. A legacy `session_create` on a context-owned policy keeps its
catalogue; it does not revert to global filters.

A new context contains only standard filters, standard Datalog and the reference
backend. It never inherits or seals legacy modules. Multiple contexts may reuse a
filter name with different semantic IDs and behavior without interfering.

**Scope:** domain registration is still process-wide. Manifest loading remains
on the legacy path; this API adds contextual inline/frontend loading, not a new
manifest format. Python and JavaScript do not yet expose catalogue selection.
The native C API is the first integration surface.

## WASM and dynamic loading

There is no `dlopen`, side-module ABI, hot reload or plugin search path.
For a concrete WASM extension, compile its C implementation into the same
module and call its registration from the application's initialization before
loading policies. The shipped JS/WASM interface still uses the compatibility
registry and does not expose these new native context functions. Four-kind
catalogue selection in bindings is separate work, not implied by static linkage.

`bash tools/check_wasm_extensions.sh small` (or `large`) builds each of the five
C conformance examples with the full C host into one WASM module and runs it
under Node. This verifies same-module linkage independently of the JS wrapper.

A dynamic loader should only follow a concrete deployment need, with explicit
ownership, signature/version policy and platform-specific tests. It is not
needed to develop or distribute separately authored extensions today.

## Verification

[Five standalone examples](../../sdk/examples/README.md) share one workflow.
The [installed conformance kit](../../sdk/conformance/README.md) checks typed
contracts. CI tests copied-out examples against static/shared installed SDKs
in SMALL and LARGE profiles, plus context isolation/lifetime and legacy
fingerprint/proof golden fixtures. The common declaration does not loosen IR
validation, change Datalog grammar or replace the open reference solver.
