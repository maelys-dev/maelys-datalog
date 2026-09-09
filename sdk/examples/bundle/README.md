# Frontend + filter bundle

## Contract

One package declares two cooperating components:

- The `permit` frontend lowers a small authorization language to public IR.
- The `exact_match` filter compares bytes during execution by the reference solver.

```text
# Accounts that may be allowed
permit "alice"
permit "carol"
```

The first directive produces the equivalent of:

```prolog
allow(X) :- candidate(X), exact_match(X, "alice").
```

The application registers a domain with unary `candidate` (EDB) and `allow`
(IDB + QUERY), then supplies candidates as input facts. Given `alice`, `bob`
and `carol`, only `alice` and `carol` are allowed. A directive never creates
a candidate or bypasses the host's domain and IR validation.

## One declaration, explicit selection

`example_bundle_extension()` returns an envelope with one frontend and one
filter; it contains neither a planner nor a backend. Register this envelope once,
seal the context with the default planner, select `permit` when loading, and
create a session with the reference backend. See `end_to_end()` in
`tests/conformance.c` for the checked, executable flow.

Registration does not activate the frontend: NULL still means standard Datalog.
The frontend emits a filter literal with both its name and semantic ID. The host
resolves that literal against the same immutable catalogue; the solver later
evaluates it through the host's budgeted filter dispatch.

This is cooperation through the public IR, not a direct frontend-to-filter
callback. There is no special bundle API or dependency loader. Removing the
filter, or substituting the same name with an incompatible semantic ID, makes
loading return `UNSUPPORTED` with no policy.

The exact-match implementation is deliberately self-contained, with the same
identity and contract as the focused filter example. Do not register both
copies in one context: duplicate component names/identities are rejected.

## Dialect and limits

One `permit "account"` directive is allowed per line. Leading/trailing horizontal
whitespace, blank lines and whole-line `#` comments are supported. Strings have
no escapes: backslashes, bytes below `0x20`, missing quotes and trailing text are
rejected. Empty patterns are accepted by the filter. The host validates UTF-8
and enforces its pattern, rule and storage limits.

Each directive carries its 1-based source line/column into the IR. Syntax errors
report their position; an error after valid directives still returns no partial
policy. This is an educational DSL, not Gitolite, regex or a change to Datalog.

## Build and test

Install the engine SDK first. From this directory, the commands are identical
to the focused examples:

```sh
cmake -S . -B build -DMAELYS_SDK_PREFIX=/absolute/path/to/sdk-install
cmake --build build
ctest --test-dir build --output-on-failure
```

Use a separate directory with `-DMAELYS_SDK_SHARED=ON` for shared-engine linkage.
The provider remains statically linked into the executable. No sibling example
sources or private headers are required.

The installed-SDK check copies this entire project outside the repository and
tests both linkage modes. The WASM check links this same provider, test and C host
into one module; it does not add a JavaScript selector.

## Conformance

- Filter kit fixtures: equality, nonmatch, empty bytes, invalid pointers and overflow.
- Explicit frontend selection, normalized IR, source locations and copied source.
- Two allowed accounts and one denied candidate, enumeration, Why-true and Why-false.
- Session lifetime after releasing the context and policy handles.
- Missing filter and incompatible filter semantic ID rejected at load time.
- Invalid frontend descriptor rolls back both registrations; retry succeeds.
- Syntax failures and an EDB rule head rejected by mandatory host IR validation.

Callbacks return statuses and never print or terminate; only tests report failures.
Contexts copy descriptors and names, not code. Change semantic IDs when observable
component behavior changes.

## Layout and license

`include/extension.h` exposes the declaration; `src/extension.c` holds both typed
components; `tests/conformance.c` verifies their integration.
This example is MPL-2.0; see `LICENSE`.
