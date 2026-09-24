# JavaScript / WebAssembly SDK

One module instance owns one `MaelysPlayground`. The C adapter includes only
`maelys/datalog.h` and links an installed Emscripten SDK. It never exposes native
engine objects, symbols for input, or internal solver exports. This breaking
migration ships with 0.10.0; the consumer C API remains 2 and backend ABI 4.

```js
const pg = await MaelysPlayground.create(MaelysDatalogDynamic, wasmUrl);
try {
  pg.registerDomain({ name: 'events', predicates: [
    { name: 'event', arity: 2, flags: PredKind.EDB },
    { name: 'total', arity: 1, flags: PredKind.IDB | PredKind.QUERY },
  ], atoms: [] });
  pg.loadPolicy('events', 'main', 'total(N) :- sum(V,event(_,V),N).');
  pg.addFacts([
    { predicate: 'event', terms: [1n, 5n] },
    { predicate: 'event', terms: [2n, 5n] },
    { predicate: 'event', terms: [2n, 5n] },
  ]).solve();
  console.log(pg.query('total', [10n])); // true: repeated complete fact deduplicates
  console.log(pg.enumerate('total', 1)); // [[10n]]
  console.log(pg.explainTrue('total', [10n]));
} finally {
  pg.close();
}
```

`PredKind`, `Status`, `DatalogError` and `MaelysPlayground` are CommonJS exports
and browser globals. The distributed `.d.ts` describes the same surface. JS,
glue, Wasm and types must come from one build; transport version 1 and consumer
API 2 are checked before opening an instance. There is no compatibility shim.

## Values and authority

Input terms are `string | bigint | number | boolean`, arity 0–4. Numbers must
satisfy `Number.isSafeInteger`; use bigint for the full signed int64 range.
Every integer output is bigint, even zero. Strings, integers and booleans have
separate identity. Unsupported types, fractions, infinities, unsafe numbers,
embedded NUL and unpaired UTF-16 surrogates are rejected before insertion.
Empty strings and valid Unicode are preserved. Capacity lengths count UTF-8
bytes, not UTF-16 code units. JSON.stringify does not serialize bigint: format
integers deliberately instead of coercing them to number.

Transporting int64 does not extend policy syntax or arithmetic. Numeric
aggregates still require values in 0..2147483647; sum overflow rejects solving.
`count` counts distinct typed projections; `sum` counts each distinct full
source fact. The native SDK remains the authority on domain, kind and capacity.

`registerDomain` copies the predicate declarations and explicit policy-source
`atoms`. Runtime strings require no atom declaration. A symbolic constant in
policy source does; writing a fact in source also requires POLICY_FACT origin.
No automatic atom discovery or permissive inline loading is performed. Domain
registrations persist for the lifetime of their module, including after close.
A fresh module gives an independent registry.

## Lifetime and failure

- `loadPolicy` compiles inline source and creates a reference session; success
  clears input, failure preserves the previous policy/session/input. It refuses
  while a result is live.
- `addFacts` appends ONE atomic batch, including multiple predicates. It copies
  text/values. Shape and raw storage are checked at append; declared predicates,
  arity and native per-predicate/symbol budgets are checked at solve. An accepted
  append is not a promised successful solve. Capacity counts raw entries before
  deduplication; no silent batching or heap fallback expands it.
- `solve` recomputes the complete input. At most one result is live; a second
  solve or input mutation is refused until `freeResult`. A failed solve keeps
  the input but publishes no result. `clearFacts` then permits correction.
- `freeResult` releases the result only; `clearFacts` clears input only. Returned
  enumeration arrays and strings are ordinary independent JS copies. No public
  raw IDs or borrowed Wasm views survive a call.
- `close` releases result, session, policy, input and conversion scratch. All
  operations after close fail. Close is explicit; there is no finalizer. Two
  wrappers cannot claim the same module, even after its wrapper closes.

`buildLimits` reports the twelve public scalar limits. `inputUsage` reports raw
fact count, occupied interned-text bytes and configured text capacity.

`query` observes EDB, policy facts and IDB for authorized predicates.
`enumerate` returns only derived IDB facts for its authorized predicate;
`derivedFactCount` counts all derived predicates, including non-queryable ones.
Unknown symbols produce false membership without interning; predicate/arity
errors remain errors and take precedence over unknown-symbol absence.

## Explanations and diagnostics

`explainTrue` and `explainFalse` return canonical text unchanged. No other method
calls them implicitly. Known absent facts give `not-derived` for Why-true;
Why-false on present facts gives `not-applicable`. An unknown symbol yields
NOT_FOUND for explanations, even though membership returned false. Truncated
Why-false is an incomplete search, not a complete proof of non-derivability.
Measure/write and short-buffer retry preserve the public C contract.

Native failures throw `DatalogError` with `.status` and an immutable `.diagnostic`
snapshot. It carries source, code/name, status/name, a bigint presence mask,
phase/message/hint and only the present optional sections. Predicate, capacity
and depth may coexist. The returned status remains authoritative when no
structured detail exists. No stale diagnostic from an earlier operation is
used; no padding or bytes after a string's NUL are serialized. Local JS argument
errors are TypeError/RangeError, not fabricated engine diagnostics.

`fingerprints()` returns policy, session and execution fingerprints. Execution
identity includes build profile/options/backend, not EDB data; these values are
identities, not proofs that a computation was performed correctly.

## Memory and build contract

`open` allocates exactly one bounded reusable C conversion scratch and creates
one default-capacity input EDB (one allocation). Its sizes follow loaded SDK
limits. There is no per-fact native transport allocation. Loading/session
creation allocate separately. The reference engine's add/solve/query/result
release path remains allocation-free after creation; the native allocation
probe forbids all engine allocator calls during those operations.

JS arrays/strings and per-call transport frames allocate. Text explanations use
the public allocating convenience path and prepare on measure and write; no
large session explanation workspace is silently reserved. `close` frees owned
allocations. Ordinary reset/free is not secure erasure. Wasm linear memory may
grow and need not shrink after free: SMALL/LARGE bound engine capacities, not
browser memory. This is not a zero-malloc binding or an elastic engine profile.

```sh
for profile in small large; do
  make -f Makefile.wasm maelys_datalog_dynamic.js WASM_PROFILE="$profile"
  bash tools/check_wasm_binding.sh "$profile"
done
```

The builder installs a target SDK in a fresh prefix, clears ambient include
paths, compiles the adapter outside the repository, and runs a separate public
C oracle. Negative controls reject private includes and consumer API 1/3 with
otherwise identical headers. Node tests are then copied outside the repository
with the built wrapper/glue/Wasm. The exports are inspected in the actual Wasm
module. `ASSERTIONS=1` preserves named exports across supported Emscripten
versions and keeps runtime assertions; no timing improvement is claimed.

Only `maelys_datalog_dynamic.js` remains as a product target; historical native
core/examples JS targets are removed. Native extension conformance executables
remain independent SDK tests. The TypeScript declaration is mandatory in the
Wasm release package. Native header archive cleanup remains A4.

## Migration from the previous wrapper

| Previous surface | Replacement / changed contract |
| --- | --- |
| domainBegin/AddPredicate/Commit/Abort | registerDomain({name,predicates,atoms}); flags replaces kindFlags |
| loadRuleset | loadPolicy; failure preserves the previous loaded policy |
| edbBegin | freeResult, then clearFacts when starting a new snapshot |
| addFact/addFact2, symbol unary/pair batches | addFacts([{predicate,terms}, ...]); heterogeneous atomic batch |
| internRuntimeSymbol, all addSymbolId(s)* methods | Removed; submit values, never IDs |
| querySymbol/querySymbol2 | query(predicate, terms), typed arity 0–4 |
| enumeratePredicateFacts / symbolText | enumerate returns copied resolved values; integers always bigint |
| explainFactText | explainTrue; no null for absent explanations; unknown symbols are NOT_FOUND |
| no Why-false operation | explainFalse, preserving incomplete/not-applicable states |
| diagMessage/diagHint/diagCode | DatalogError.diagnostic snapshot, with independent sections |
| freeResult reset input implicitly | Releases result only; clearFacts explicitly resets input |
| Wasm ruleset pointer / raw engine exports | Removed; no replacement |
| no owner cleanup | close explicitly releases all owned handles |

Retained operations are solve, freeResult, derivedFactCount and buildLimits.
Bundles, windows and backend selection are separate tranches, not hidden in
this adapter or required for this migration.

The CI TypeScript consumer checks exact integer outputs and rejects removed methods
and symbol-ID inputs. A runtime test also compares the complete method and constant
surface with the shipped declaration. Type check locally with:

```sh
npm exec --yes --package=typescript@5.9.3 -- tsc --strict --noEmit --target ES2020 --module commonjs tests/wasm/types.ts
```

Aggregate failures retain `INVALID_FIELD` with `solve_aggregate_domain_error`
(operand outside the integer domain) or `solve_sum_overflow` (valid operands,
overflowing sum). `diagnostic.aggregate` contains operator, value text, IR value
kind, zero-based source argument index and inclusive numeric limit. Integer text
is exact even outside JavaScript's safe-number range; symbol text is bounded.
The numeric bound is separate from storage-capacity diagnostics.
