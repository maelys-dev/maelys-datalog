# Compatibility freeze review — 0.22.0 candidate

This review precedes the separate freeze declaration and the proposed 0.22.0
release. It is an evidence inventory, not approval to merge, tag or publish.
The freeze does not require a 1.0.0 version. Published v0.21.0 remains the
baseline until the next release is explicitly approved.

## Boundaries under review

The [compatibility contract](../compatibility.md) covers installed declarations,
public record layouts, negotiated backend ABIs, observable semantics and
lifetimes. The candidate also defines the Python binding's exported and
documented public surface; its exact revision still needs release review.
Private storage sizes, offsets, compiler placement and measured
latency are not frozen. Caller-owned consumers re-query requirements for the
installed SDK, actual program, options, profile and provider.

The three four-term/capability/accessor clauses in that contract remain intact.
They permit new opt-in record families and entry points; they do not promise
wider relations in the current records. Unknown capabilities and unrepresentable
inspection content are refused explicitly. Backend ABI 5, 6 and 7 remain
independently negotiated. Allocation services do not silently change a fixed
provider's contract, and ABI 7 input delivery does not waive full-output or
commit/abort obligations.

## Existing evidence and its scope

| Contract area | Evidence retained in the repository | Scope and limits |
| --- | --- | --- |
| Installed header families and source migration | [SDK headers](../sdk-headers.md), [0.18.0 review](v0.18-release-review.md) | Installed declarations and independently compiled consumers; historical header renames are not undone. |
| Fixed capacities and provider negotiation | [Session resources](session-resources.md), [0.14.0 review](v0.14-release-review.md) | Program/build bounds stay distinct from session quotas; each ABI requires its own admission. |
| Retained inputs, introspection and ABI 7 | [0.16.0 changelog](../../CHANGELOG.md), [validation inventory](../validation.md) | Transactions retain typed input semantics and rollback; an external verifier is a consumer, not a new engine service. |
| Both window adapters and optional allocation | [Allocation qualification](../backend-allocation-evidence.md), [allocator contract](../proposals/backend-allocation.md) | Public conformance providers, independent charges, provisional ownership, leases and late rejection. This is not qualification of an independent private backend. |
| Effective query inspection and policy identifiers | [0.20.0 review](v0.20-release-review.md) | Published accessor contracts and installed consumers, including effective query whitelists. |
| Empty sets and compact private facts | [0.21.0 review](v0.21-release-review.md) | Public records and fingerprints preserved; a live empty set has OK/zero count and a defined fingerprint. Private storage plans may change. |

These records identify their original revisions, runs and decisions. They are
historical evidence, not claims that their tests or timing acceptance already
qualify the new candidate. A provider fixture does not demonstrate every
independent provider's algorithm, allocation guarantee or language coverage.

## Real installed-SDK consumers

The table records the consumer revision actually qualified, not an untested
latest main. Each dependency pin was checked at that revision against published
v0.21.0 (`9630f591637c7046ec73996458e1ba282194e32c`). The linked logs show SDK
installation before external consumer builds. Dates are UTC. This inventory
does not requalify these consumers against the unpublished 0.22.0 candidate.

| Consumer repository | Qualified pinned consumer commit | Installed SDK | Qualification result and evidence | Date |
| --- | --- | --- | --- | --- |
| [maelys-datalog-cli](https://github.com/maelys-dev/maelys-datalog-cli/pull/9) | `4be8e34450bacdba8bafaf8d2c62983d47299eb4` | v0.21.0 | [37111243252](https://github.com/maelys-dev/maelys-datalog-cli/actions/runs/37111243252): four active checks green; installed SDK behavior/schema tests and 286 conformance cases passed. SMALL/LARGE and sanitizer replays are recorded in the PR. | 2026-10-03 |
| [maelys-datalog-incremental](https://github.com/maelys-dev/maelys-datalog-incremental/pull/39) | `9343dc9c23cbcf4a08eb8239ef3525529de6c10c` | v0.21.0 | [37113056663](https://github.com/maelys-dev/maelys-datalog-incremental/actions/runs/37113056663): installed guard/sanitizer SDKs, LARGE 12/12 and SMALL 2/2; separate opt-in elastic probe 1/1 in both profiles. This completed qualification run does not imply that every general CI job or the PR merge has completed. | 2026-10-03 |
| [maelys-datalog-verifier](https://github.com/maelys-dev/maelys-datalog-verifier/pull/9) | `e23845be860691301a5036ca3eeb40cc84df2bef` | v0.21.0 | [37111216556](https://github.com/maelys-dev/maelys-datalog-verifier/actions/runs/37111216556): four active checks green; 15/15 suites in each profile on the ordinary targets and under sanitizers, including live empty/disabled manifests. | 2026-10-03 |

These are separate SDK consumers. Their scopes and rejected programs remain
those of their own qualification; successful SDK integration is not a claim of
universal language support, verifier completeness or measured latency.

## Named unknown-capability refusal

`unknown_backend_capability` in `tests/test_maelys_datalog_context.c` is also
registered as `unknown_backend_capability_static` and
`unknown_backend_capability_shared` in CMake. It uses an unknown **language**
capability bit (63), independently of the existing resource-feature-bit test.
Copied valid ABI 5 and ABI 6 descriptors with that bit are refused at extension
registration with INVALID_ARGUMENT and leave catalog counts unchanged. Clearing
only that bit makes the same descriptors register successfully.

A session requiring that bit is refused by both `session_create_ex` and the
context creation path; neither publishes a session. Configuration rejects it
without changing its required-capability mask. A known-capability session then
creates and closes normally. These positive controls distinguish the intended
refusal from an otherwise invalid descriptor or policy. ABI 7 has no extension
registry in its current contract; this test does not invent or claim one.
There is no runtime or public-interface change in this witness.

## Final handle-liveness correction

[#180](https://github.com/maelys-dev/maelys-datalog/pull/180), signed head
`6bd4ea6973a72fd931b3fed07fd0e22888927edb`, restores consistent status after
freeing an addressable caller-owned policy handle. Clearing caller-owned storage
also clears its released flag; a zero reference count must therefore reject
access before an index lookup. One internal predicate governs count, id,
fingerprint, statistics and session creation, including sizing/configured paths.
This does not make a freed engine-owned pointer valid or synchronize concurrent
release.

The named `caller_owned_policy_liveness` witness frees an empty manifest handle,
checks INVALID_STATE for all five operations and their output contracts, and
reloads the same storage. Two freshly rebuilt mutants restore the defective
released-only check in id and statistics respectively. Each is caught by its
own assertion, after a passing baseline. Both profiles pass locally; compilation
failure is never accepted as mutation detection.

Local qualification on that head: SMALL and LARGE `make check`; CMake Debug SMALL
and Release LARGE under ASan/UBSan/alignment, 39/39 each; allocator guards;
the two negative controls in both profiles; socle v0.62.2 check. These runs are
macOS functional evidence, without a LeakSanitizer or performance claim.
[Hosted CI 37114217056](https://github.com/maelys-dev/maelys-datalog/actions/runs/37114217056)
passed all 46 checks on that head. The authorized squash merge is signed commit
`5f668293241a0070202b06f177d2c87ea2318f2e`. Successor and integrated-main checks
must still qualify their own heads.

## Remaining decisions before the release

The Python owner-reference cleanup is a prerequisite for 0.22.0: integrate
`Ruleset._engine` and `Edb/Session/SolveResult._ruleset`, with every internal
access updated and no former-name aliases, before cutting the freeze release.
The candidate is prepared on `fix/python-private-owners`. Its pull request
must record the signed revision and the SMALL/LARGE installed-SDK Python
checks, including the ownership-shadowing witness. Record its integrated
revision and qualify the final binding candidate separately; the existing
release evidence in this review does not cover this rename.
Review the exported names and documented public members at that same revision
to establish the Python baseline. This cleanup must not be deferred to a
post-freeze release.

1. Review and authorize the handle correction, then integrate and qualify each
   documentation successor on current main. A stacked branch's ancestor checks
   do not qualify its integrated head automatically.
2. Obtain and read the complete Python lifecycle report for the final runtime
   candidate. Record measured commit, run, report SHA-256, retained alerts,
   controls and David's decision in the release/changelog PR. Earlier accepted
   reports remain historical; flat native counts do not approve new timings.
3. Verify the final installed SDK and binding/packaging checks in both profiles.
   Preserve the caller-storage requirements, statuses, leases, rollback and
   separately compiled provider matrix. Documentation alone adds no runtime
   behavior, but workflow success is not the release decision.
4. Authorize the freeze declaration and release ceremony separately. The dated
   0.22.0 changelog and version/tag PR are not created by this review. The pinned
   socle's check, preflight, rehearsal, signed cut and publication gate still
   apply. Published archives, receipts and attestations are verified afterward.

No remaining item is waived by describing the contract as frozen. The next
minor version may carry the freeze only after these decisions and checks.
