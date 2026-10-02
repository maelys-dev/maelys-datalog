# Optional backend allocation: qualification and measurement

The installed allocator contract is specified in `proposals/backend-allocation.md`.
This protocol qualifies the public conformance provider, not an independent
backend. The provider implements bounded projection and anti-join rules with
real retained and provisional blocks. Host E/D/S/T quotas remain fixed.

## Functional evidence

`test_maelys_datalog_backend_allocation` covers admission, separate engine/caller
allocation guards, checked charge arithmetic, inspection, exact boundaries,
coexistence, sticky failures, ownership rollback, reuse, identity and leases.
`test_maelys_datalog_allocation_windows` exercises both real adapters with ABI 6
and ABI 7: equal caps, independent charges, shared caller allocator, alternating
banks, duplicate supports, static replacement, expiry, negation-driven output
growth, each acquisition failure and late explanation-lease rejection.

`tools/check_backend_allocation_history.sh` builds old and new callers/providers
separately against installed SDKs. Python independently reproduces the contract
vectors and the actual installed consumer's elastic identities. Static and shared
SDK consumer checks exercise both suites. The mutation tool has twelve service
and six window mutations, each rebuilt under ASan/UBSan and required to fail a
named assertion after a passing baseline. Build failures are not detections.

## Declared software-count campaign

Use the manual `bench-compare.yml`, `allocation_counts=true`, full base SHA for
v0.18.0 and full candidate SHA. One sequential hosted Ubuntu job builds both
profiles, every SDK and every consumer before the first Callgrind process. No
release token, timing gate or PR trigger is involved. `compare_backend_allocation.py`
requires a clean checkout of the exact candidate and records compiler, CPU,
platform, source hashes, binaries, symbol tables and disassembly.

For each profile, the ordinary path uses the 7/93-symbol policies, inputs and
queries of `python_workload.py`, in prepared and convenience modes. Fifty warmup
requests precede 200 counted requests per case. Two A/A pairs per revision precede
A B A B, each pass in a separate process. Counts cover input reset/append, native
solve, workload queries and result release; convenience includes input/session
creation and closure. Preparation and full typed-output validation are excluded.
This is native evidence for that workload, not a Python measurement.

The elastic matrix has ABI 6/7 at 8/64 users: accepted replacement, rejected third
caller acquisition, alternating growth/shrink (N/2 to N), and late expiry rejection in
both window adapters after growing anti-join output. Each of the twenty cases
runs 200 operations in two independent processes. Old/new coexistence, release,
abort cleanup, caller malloc/free and service accounting remain inside the region.
Windows retain the old result through an explanation lease; release of that lease
and a successful expiry retry are checked outside the measured failed region.
The peak of the operating bank is added to the other bank's current reservation,
identified by acquisition activity, not by historical peaks.

The reporter requires identical checked outputs, Ir/Dr/Dw totals, exclusive
function counts and residuals across every repeated region. Every nonzero
ordinary-path delta is retained by function; there is no automatic tolerance.
Provider, named service functions, and remaining host/shared/driver costs are
reported separately. Inlined service work stays in the latter category; all
attribution residuals remain visible. These are software counts, not cycles,
physical traffic, cache evidence or latency.

A separate telemetry build wraps explicit C memcpy/memmove/memset/strcpy calls.
It reports byte requests only and must reproduce the same receipts and allocation
charges. It is never used for instruction or time comparisons: instrumentation
can retain otherwise dead writes and does not count implicit structure assignments.
Memory receipts include fixed reservation, before/after charge, operation peak,
per-bank cap, configured aggregate bound and acquisition/release counts. Policy,
caller-owned retained-input handles and explanation buffers are outside this
service-cap formula and are not represented as free memory.

All raw outputs and generated reports stay in run artifacts. The complete Python
report, with positive and identical-binary controls, remains a separate requirement
for the final candidate; a maintainer decision on that report must precede release.
The allocator contract remains revisable before a compatibility freeze and awaits
an independent installed-SDK consumer prototype after publication.

## Hosted candidate evidence

The [instruction run 36982088120](https://github.com/maelys-dev/maelys-datalog/actions/runs/36982088120)
measured signed `0d1688e74abbcba2c9c55ec5fdee3394c9f7fa71` against
v0.18.0 (`3173f87beb5eb99576bb9c55ff7c88facf180877`), with Clang 18.1.3,
Release SDKs and an AMD EPYC 7763 runner. The artifact is
`backend-allocation-0d1688e74abbcba2c9c55ec5fdee3394c9f7fa71`;
`report.json` SHA-256:
`7d9438f03d1d6b2f91e5db69fea1203f70fe520280afc1c6fe0d9127cb705a2c`.
All 176 scoped regions are present: 96 FIXED and 80 elastic. The 120 repeated
region comparisons agree exactly in receipts, Ir/Dr/Dw, exclusive functions and
residuals. Rebuilding the report from downloaded raw data reproduces its bytes;
eight binary hashes and all manifest source hashes were checked.

| Profile | Mode | Extra Ir/request, both 7/93 facts | Extra Ir at 93 facts |
| --- | --- | ---: | ---: |
| SMALL | prepared | 32 | +0.002129% |
| LARGE | prepared | 25 | +0.001652% |
| SMALL | convenience | 52 | +0.003106% |
| LARGE | convenience | 45 | +0.002569% |

The extra work is not zero. Exclusive function deltas account for every extra
instruction: `session_solve_materialized` +15 and `maelys_datalog_result_free` +10
per request in both profiles. SMALL adds +7 in `__memset_avx2_unaligned_erms`,
called from `solve_stratified_path`. Convenience adds +4 in
`maelys_datalog_session_free` and +16 in `__memcpy_avx_unaligned_erms`, called
from `maelys_datalog_prepared_session_init_sized`. These identify the executed
paths, not a hardware mechanism or a latency effect. No fixed allocation/reset
budget or instruction tolerance was widened.

All forty elastic case/profile receipts return to their initial current charge.
Each 200-operation region attempts 600 caller acquisitions: accepted and late
window rejection paths perform 600 releases; refusal of the third acquisition
per operation performs 400 releases because the refused request owns no block.
For example SMALL ABI 6, 64 users in a group window, late expiry rejection has
690,042 current bytes before/after and a 767,016-byte coexistence peak. Its 200
attempts request 2,138,600 explicit copy bytes and 7,821,200 reset bytes in the
separate telemetry build. The total is 251,989,436 Ir, including host, provider,
caller allocation and cleanup; it is not a comparison with a FIXED provider.
The ABI 7 fixture reserves a full-capacity candidate, a deliberate fixture choice,
so these receipts are not a memory-efficiency ranking of ABI 6 and ABI 7.

The complete Python run is separate. David accepted run 36982091628 for 0.19.0
on 2026-10-02 after review; its original classifications remain unchanged. See
[the release review](validation/v0.19-release-review.md) for the report digest,
observations and decision. The instruction report and successful CI alone do not
approve a performance tradeoff or release.
