# Compatibility contract for the freeze

This page records the intended compatibility boundaries for the freeze review.
It does not announce a release or freeze a development version. Published SDK
archives and tags remain immutable. A release decision remains separate from
this contract and from its validation evidence.

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

## Evidence before the freeze

The freeze review must verify installed-SDK consumers, separately compiled callers
and providers for each supported ABI, capability rejection, storage boundaries,
borrowed lifetimes, typed results and diagnostics. Language/binding parity and
release performance review remain separate evidence. Internal byte-size or
instruction improvements do not replace those checks.
