# Predicate declaration and storage contract

Available in 0.10.0. This declaration migration is part of the coordinated
[0.10.0 API migration](../api-type-migration.md).

Every input predicate table uses `maelys_datalog_predicate_t` from
`<maelys/datalog.h>`:

```c
static const maelys_datalog_predicate_t predicates[] = {
    {.name = "edge", .arity = 2, .flags = MAELYS_DATALOG_PREDICATE_EDB},
};
```

Both `maelys_datalog_domain_t` and the advanced
`maelys_datalog_domain_def_t` accept that same array. The advanced inline loader
also accepts it. The stable domain remains declarative; the advanced domain
still offers its existing installer callback and descriptive metadata. No cast
between domain types is valid or necessary.

## Ownership and bounds

Registration copies domain/predicate names, atoms and optional metadata into
bounded engine-owned storage. Predicate names remain limited to 63 bytes plus
NUL. Input tables and strings only need to survive the registration call;
stack arrays and string literals are both valid. A pointer in a declaration
does not imply allocation, unbounded names or permanent borrowing.

The registry stores `maelys_datalog_predicate_entry_t`, with an inline
`char name[64]`, arity and kind flags. It is private storage, not a second input
declaration. `maelys_datalog_domain_registry_find` returns a private
`maelys_datalog_domain_entry_t` view of owned entries. These views cannot be
passed back as declarations. The global domain storage, predicate registry and
ruleset retain their existing member layouts; no persistent pointer-view table
is added alongside the owned names.

Registration validates its candidate before publication; an unsuccessful new
registration consumes no slot. This does not change low-level **installation**:
installation into an existing predicate registry may leave partial changes on
error. Policy loaders discard that candidate. Callback code must outlive any
later policy load using it.

Stable duplicate-domain registration still checks the ordered declarations;
low-level registration still keeps the first declaration. Unifying the input
type does not unify these functions' existing validation or error contracts.

`maelys_datalog_program_predicate` already returns the common public shape.
That is a borrowed read-only view whose name lives with the program. This
output contract is distinct from registration's synchronous copy contract.

## Complete migration

- Replace declaration tables of `maelys_datalog_predicate_def_t` with
  `maelys_datalog_predicate_t`, and their `kind_flags` field with `flags`.
  Never replace the name pointer with an inline array or alias either layout.
- Rebuild all advanced C consumers: input-array stride changes, and the private
  return type of `domain_registry_find` is now a stored entry view. There is no
  compatibility typedef or dual declaration path.
- Python uses the common public declaration directly through CFFI. The shim
  and `maelys_py_predicate_def_t` are removed. Use `Predicate.flags` (formerly
  `kind_flags`) or named constructors, and rebuild against the installed SDK.
- Wasm now consumes the installed public SDK through its typed API. The old
  native-object builder and raw exports are removed without aliases; see the
  [Wasm migration](../../bindings/wasm/README.md).
- The declaration migration preserves stable C declaration layouts, language
  syntax, fact semantics and memory profiles. The coordinated 0.10.0 diagnostic
  migration separately requires consumer API 2, backend ABI 4 and program ABI 2.
  This does not freeze future interfaces or establish a whole-engine zero-heap
  guarantee.

## Validation

SMALL and LARGE run the same native, installed SDK and binding inventories.
The whole-engine allocator guard exercises registration through both domain
entry points with allocation disabled: mutated caller strings, duplicate and
conflicting registrations, 63/64-byte names, partial candidate rejection and
retry, predicate capacity and global slot exhaustion. The callback/atom tests
remain part of the domain registry suite. ASan/UBSan cover both profiles.
