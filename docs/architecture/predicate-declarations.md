# Predicate declaration and storage contract

Status: next minor release, targeting 0.10.0; not part of published 0.9.0.

Every input predicate table uses `maelys_datalog_public_predicate_t` from
`<maelys/datalog.h>`:

```c
static const maelys_datalog_public_predicate_t predicates[] = {
    {.name = "edge", .arity = 2, .flags = MAELYS_DATALOG_PREDICATE_EDB},
};
```

Both `maelys_datalog_public_domain_t` and the advanced
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
  `maelys_datalog_public_predicate_t`, and their `kind_flags` field with `flags`.
  Never replace the name pointer with an inline array or alias either layout.
- Rebuild all advanced C consumers: input-array stride changes, and the private
  return type of `domain_registry_find` is now a stored entry view. There is no
  compatibility typedef or dual declaration path.
- The Python C shim uses the public type too; `maelys_py_predicate_def_t` is
  removed and its CFFI declaration and layout guard use `flags`. Rebuild the
  shim and CFFI module together. Python's high-level `Predicate.kind_flags`
  remains the same API and maps to the common C field.
- The Wasm builder still copies each name when it is added. Its fixed staging
  storage owns names across calls. Commit constructs a temporary bounded array
  of common declarations, which registration copies before returning. This adds
  at most `MAX_PREDICATES * sizeof(public_predicate_t)` automatic bytes during
  commit; persistent builder storage and exported JS/Wasm signatures are unchanged.
- Stable C declarations/layouts, backend ABI 3, program ABI 1, language syntax,
  fact semantics and memory profiles are unchanged. This does not freeze ABI 4
  or establish a whole-engine zero-heap guarantee.

## Validation

SMALL and LARGE run the same native, installed SDK and binding inventories.
The whole-engine allocator guard exercises registration through both domain
entry points with allocation disabled: mutated caller strings, duplicate and
conflicting registrations, 63/64-byte names, partial candidate rejection and
retry, predicate capacity and global slot exhaustion. The callback/atom tests
remain part of the domain registry suite. ASan/UBSan cover both profiles.
