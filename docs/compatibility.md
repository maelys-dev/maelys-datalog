# Compatibility contract for the freeze

The compatibility freeze defined here takes effect with the explicitly approved
publication of 0.22.0. Until that publication, this declaration is a candidate;
merging documentation alone does not freeze a development build or approve a
release. The project keeps its 0.x version series: the freeze is a compatibility
commitment, not a promise to publish 1.0.0. Published SDK archives and tags remain
immutable. The [freeze review](validation/freeze-review.md) records evidence and
remaining decisions separately.

## Frozen baseline: versions and record bounds

The existing installed contract has the following numeric baseline. These are
versions and bounds of the current interface/record families, not a prohibition
on explicitly negotiated future families or additional headers.

| Contract | Frozen value | Installed declaration |
| --- | ---: | --- |
| Consumer API | 2 | `MAELYS_DATALOG_PUBLIC_API_VERSION` |
| Compiled-program ABI | 2 | `MAELYS_DATALOG_PROGRAM_ABI_VERSION` |
| Backend ABIs, negotiated separately | 5, 6, 7 | `MAELYS_DATALOG_BACKEND_ABI_VERSION`, `MAELYS_DATALOG_BACKEND_V6_ABI_VERSION`, `MAELYS_DATALOG_BACKEND_V7_ABI_VERSION` |
| Terms per current fact/view/IR atom | 4 | `MAELYS_DATALOG_PUBLIC_MAX_TERMS` |
| Body literals per current IR rule | 8 | `MAELYS_DATALOG_IR_MAX_BODY` |
| Variables per current IR rule | 32 | `MAELYS_DATALOG_IR_MAX_VARIABLES` |
| Existing installed header family | 11 | The paths listed below, detailed in [SDK headers](sdk-headers.md) |

The eleven existing paths under `maelys/` are `datalog.h`,
`datalog_builders.h`, `datalog_window.h`, `datalog_inputs.h`,
`datalog_resources.h`, `datalog_program.h`, `datalog_explanations.h`,
`datalog_extension.h`, `datalog_module.h`, `datalog_frontend.h` and
`datalog_backend.h`. Their continued compatibility does not resurrect retired
header paths. Program/build limits that differ by size profile and effective
session quotas remain distinct; the numbers above do not replace admission or
storage-plan queries.

## Public records and language features

The four-term limit is the width of the current record family (public facts, fact views and IR atoms), not a promise about the language. A later version may admit wider relations through new capability bits and new entry points; the existing records remain valid for every program that does not require them.

A consumer treats any capability bit it does not know as unsupported, and refuses rather than guesses.

A read accessor may return UNSUPPORTED for content its record cannot carry; callers check the status before reading the record.

These rules describe future extension boundaries, not an implemented wider
record family. The currently installed headers define the admitted record
widths and available capabilities. Applications do not infer wider support
from a version number, a backend name or an unchanged callback signature.
Inspection APIs report required program capabilities; the selected provider
must admit them explicitly. Consumers retain and check the status of every
inspection call before using its output.

## Consumer API and provider ABI

The installed [SDK headers](sdk-headers.md) separate application, integration
and component-author declarations. Public values, fact records, borrowed views
and program IR are the source and binary interface; private engine records,
their sizes and their offsets are not. An internal storage change must preserve
public records, observable semantics and fingerprints unless a separately
reviewed contract explicitly changes them.

Backend ABI 5, 6 and 7 are negotiated independently. ABI 6 adds normalized
session resources; ABI 7 adds transactional input delivery. Neither allows an
application to reinterpret ABI 5 program/build bounds as session quotas.
An extension must use the ABI and capabilities it actually implements. A
provider's support for one ABI does not imply support for another.

Caller-owned storage is sized and aligned through the installed requirements
and storage-plan APIs. Consumers query them for their actual program, options,
profile and provider; they do not hard-code private sizes or reuse a plan from
another SDK build. A new library may change these requirements while preserving
public record layouts. Insufficient storage is rejected before publication.

## Python binding public surface

The candidate 0.22.0 freeze also covers the public Python binding. Its baseline
is the published 0.22.0 tag: exported names listed in
`bindings/python/maelys_datalog/__init__.py`'s `__all__`, and the public
constructors, methods, properties, fields, enum members and language protocols
documented for that release. Their documented signatures, keyword parameter
names, return values, errors and lifetime behavior are compatibility contracts.
The [binding reference](../bindings/python/README.md) records those contracts;
documentation of an older release does not qualify the new baseline. The
release review must identify the reviewed public surface at the exact candidate
revision before publication.

The binding reference's [constructor table](../bindings/python/README.md#public-constructors-and-returned-objects)
defines construction separately from export or dataclass status. `Engine`,
`Edb`, `Predicate`, `SessionCapacities` and `InputBase` have public constructors.
`Limits`, `ProgramCounts`, `Diagnostic` and `ResultTerm` are returned snapshots or views;
their generated dataclass constructors are internal. The table also identifies
the access routes for native resources and exceptions. Review this table before
cutting 0.22.0; callable implementation constructors do not enlarge the frozen
surface by accident.

A name without an underscore is not sufficient to establish a public member.
Undocumented ownership bookkeeping is internal. Before publishing 0.22.0,
`Ruleset.engine` must become `_engine`, and `Edb.ruleset`, `Session.ruleset`
and `SolveResult.ruleset` must become `_ruleset`, without former-name aliases.
Constructor parameters keep their documented names. This pre-freeze cleanup
must ship in 0.22.0; removing a frozen public member later requires the explicit
compatibility decision and migration plan described below.

Private implementation modules, underscore-prefixed ownership links and
undocumented incidental instance attributes are outside the frozen surface.
The binding provides no public owner-navigation accessor. A later public
accessor would be an explicit API addition with its own documented contract.

## JavaScript and TypeScript error construction

The Node and WebAssembly implementations share the public TypeScript declaration
in `bindings/javascript/src/index.d.ts`. Unlike the Python binding, this API
deliberately exposes `new MaelysDatalogError(operation: string, status: number,
diagnostic: Diagnostic)`. The constructor has been public since 0.15.0; native
binding operations can also throw the same exception. Python's internal error
constructor is not a parity requirement for this operation.

At the 0.22.0 freeze, the public constructor signature and its documented
behavior become part of the JavaScript/TypeScript compatibility baseline.
The binding may change how it creates errors internally, but it must preserve
this callable constructor for consumers, including its parameter names and
types, unless a later explicit compatibility decision provides a migration.

## Observable behavior and lifetimes

Canonical typed facts, identities and program/execution fingerprints are
behavioral contracts, independent of private record packing. A failed input or
window transaction preserves the previously accepted state. Result and
explanation leases govern reuse of borrowed data; callers release explanations
before releasing or reusing the result they lease.

The reference implementation's documented allocation guarantees apply to the
specified prepared paths. External providers and callbacks must establish their
own guarantees; ABI compatibility alone does not establish zero allocation,
performance or support for every window operation.

## Additive evolution after the freeze

For the frozen record families and negotiated ABIs, later releases preserve
existing declarations, layouts and documented behavior for programs that do not
request new features. New capabilities, record families, entry points and ABI
versions are negotiated explicitly; existing callers and providers do not gain
new obligations merely because another version is available. Source migration
from earlier pre-freeze releases remains documented rather than retroactively
made compatible.

A correction that restores a documented contract is identified in the changelog
and qualified against its observable results and diagnostics. A proposed change
to a frozen contract requires an explicit compatibility decision and migration
plan; a minor version number alone does not authorize that change. Performance
observations and provider-specific allocation guarantees keep their own evidence
and limits. Neither private implementation bytes nor fixed latency is promised.

A policy handle is live only until release. An addressable released caller-owned
handle returns INVALID_STATE from count, id, fingerprint, statistics and session
creation; a freed pointer remains invalid. A successfully loaded empty set is
live, with zero enabled policies and its defined fingerprint. Caller-owned
storage can be loaded again after release under the loader's requirements; its
address alone never revives the former handle.

## Qualification and publication

The [freeze review](validation/freeze-review.md) must verify installed-SDK consumers, separately compiled callers
and providers for each supported ABI, capability rejection, storage boundaries,
borrowed lifetimes, typed results and diagnostics. Language/binding parity and
release performance review remain separate evidence. Internal byte-size or
instruction improvements do not replace those checks.
