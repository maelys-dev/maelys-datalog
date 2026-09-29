# Prepared IDB array spans: evidence and limits

Measured runtime candidate: `adf2cf3d112efc95fbb9be0a5386346d630fb0bc`, signed,
stacked on unmerged #143 (`1d6ca9534bbc8bedf47103555087d042fdf10c60`).
The change passes fact/proof arrays directly through stratum sorting and
deduplication. #142 replaced inline arrays with pointers; loading those pointers
through the result after row writes can generate redundant loads. The change
keeps the same stable insertion sort, complete fact/proof association, capacity
checks, workspace layout, allocation policy and public ABI. It adds no threshold
or alternative algorithm. Original timing alerts are not assigned to this
mechanism merely because native work can be reduced.

## Functional and allocation qualification

`make check` passes. SMALL Release CMake tests pass 29/29 with MallocScribble;
LARGE Debug passes 29/29 under ASan/UBSan. The existing stratified, provenance,
window, resource and allocation contracts exercise the changed code. The
[exact-head CI](https://github.com/maelys-dev/maelys-datalog/actions/runs/36566899080)
is 17/17 successful. Generated results remain outside Git.

## Scoped instruction experiment

Local Linux ARM64, Clang 18 Release, SMALL: five separately counted complete
`93-integer-prepared` requests at indices 0/100/192/320/500 give these exclusive
vectors for `solve_once_freeze_active_stratum`:

| Revision/variant | Ir | Dr | Dw |
|---|---:|---:|---:|
| v0.13.0 | 29,496 | 12,127 | 11,298 |
| #143 | 35,943 | 14,377 | 11,298 |
| narrower swap-local experiment | 34,535 | 14,377 | 11,298 |
| array-span change | 29,481 | 12,133 | 11,296 |

All function vectors repeat exactly; checked answers agree. Physical shared-object
attribution over the whole native library gives #143 1,180,096 Ir / 239,201 Dr /
112,973 Dw versus 1,173,500 / 236,823 / 112,971 with spans. The changed exclusive
functions are stratum freezing (-6,462 Ir/-2,244 Dr/-2 Dw) and fact equality
(-134 Ir/-134 Dr). External libc and Python work remains separate. The original,
narrower and final experiment files, compiler evidence and disassembly are
preserved locally. These are software counts, not native Mac measurements,
hosted latency results or exact equality with v0.13.0.

The original-binary x86 [diagnostic](https://github.com/maelys-dev/maelys-datalog/actions/runs/36563840404)
shows why the architecture qualifier matters: original SMALL Release freezing
has 40,386/13,086/12,829 at v0.13.0 versus 38,434/15,492/12,275 after #142/#143.
More reads need not mean more instructions. A separate fixed installed-binary
count comparison of this candidate against #143 is pending; no x86 instruction
benefit is claimed before it completes.

## Complete Python release comparison

[Run 36566938072](https://github.com/maelys-dev/maelys-datalog/actions/runs/36566938072)
measures the candidate against v0.13.0, the immutable v0.11.1 anchor and the
informative v0.11.0 reference. AMD EPYC 7763, Linux 6.17, Python 3.12.12, same
Clang/profile within each comparison; all builds precede timing. Report SHA-256:
`90ed93f170904b07ff8dc80a0fc8dd7b5c360cb546cd47b8dce612df9a5d0a1d`.

Independent verification checks 11,424 raw JSON files, 3,377,136 flattened
sample rows, 236 binary/header/binding hashes, 624 comparison rows and 624 null
rows. Every A/A floor, classification and null-screen decision is reconstructed.
All 32 fixed positive-control primary warm totals are slower in both rounds.
The report remains **review_required**: 43 warm rows, including eight complete
request rows, require review. Workflow success is not release approval.

All complete-request alerts are retained below. Deltas are candidate/reference;
rounds below their A/A floor or within the matching null envelope are still in
the original report. This table does not pool rounds or compare different runs.

| Reference | Configuration / case | Statistic | Round 1 | Round 2 | A/A floor | Null envelope | Rounds beyond both |
|---|---|---|---:|---:|---:|---:|---|
| v0.13.0 | LARGE Release / 93-integer-prepared | median | +2.02% | +1.02% | 1.85% | 1.85% | 1 |
| v0.13.0 | LARGE Release / 93-symbol-prepared | p95 | +23.10% | +0.24% | 12.12% | 1.89% | 1 |
| v0.13.0 | LARGE default / 7-integer-prepared | median | +6.34% | +5.30% | 4.35% | 4.35% | 1, 2 |
| v0.13.0 | SMALL Release / 93-integer-prepared | median | +0.66% | +2.67% | 1.75% | 1.75% | 2 |
| v0.13.0 | SMALL Release / 93-integer-solve | p95 | +3.81% | -2.90% | 0.91% | 2.09% | 1 |
| v0.13.0 | SMALL default / 7-symbol-prepared | median | +2.07% | +4.53% | 1.31% | 3.32% | 2 |
| v0.13.0 | SMALL default / 93-symbol-prepared | median | +1.64% | -0.88% | 1.13% | 0.49% | 1 |
| v0.11.1 | LARGE Release / 93-symbol-prepared | p95 | +16.50% | -5.34% | 12.12% | 1.81% | 1 |

The earlier #143 reports and the original 7-symbol burst remain distinct
observations. The local instruction reduction and any quieter later case do
not erase them, establish equal cycles, or identify a hardware cause. The
[complete #143 comparison](https://github.com/maelys-dev/maelys-datalog/actions/runs/36566987207)
is recorded separately below. This change is draft;
there is no maintainer timing exception, merge approval or release acceptance.

## Complete Python comparison against #143

[Run 36566987207](https://github.com/maelys-dev/maelys-datalog/actions/runs/36566987207)
compares the same candidate with its exact #143 parent, `1d6ca95`, using the
complete matrix. This additional comparison is explicitly not a release report.
AMD EPYC 7763, Clang 18.1.3, Python 3.12.12, glibc 2.39. Report SHA-256:
`7f607e60fa7eaaf3d1055f75ef265c83f299e5ef0e4ef6da1cbbb646aa4bbc54`.

Independent reconstruction verifies 11,424 raw JSON files, 3,377,136 sample rows,
240 binary/header/binding hashes, and all 617 comparison and 617 null rows.
All 32 fixed positive-control primary totals are detected in both rounds.
There are 43 warm review-required rows, including seven complete-request rows
(six against #143, one against the anchor). All are retained:

| Reference | Configuration / case | Statistic | Round 1 | Round 2 | A/A floor | Null envelope | Rounds beyond both |
|---|---|---|---:|---:|---:|---:|---|
| #143 | LARGE Release / 7-integer-solve | p95 | +2.45% | +1.91% | 2.09% | 2.09% | 1 |
| #143 | LARGE Release / 93-integer-solve | p95 | +4.21% | +0.72% | 1.95% | 2.50% | 1 |
| #143 | LARGE default / 7-symbol-prepared | p95 | +78.59% | +6.09% | 40.20% | 40.20% | 1 |
| #143 | SMALL Release / 93-symbol-solve | median | -0.95% | +1.60% | 0.67% | 1.37% | 2 |
| #143 | SMALL default / 7-integer-solve | p95 | +12.85% | +1.62% | 2.47% | 1.04% | 1 |
| #143 | SMALL default / 93-symbol-prepared | median | +0.97% | -0.68% | 0.63% | 0.63% | 1 |
| v0.11.1 | LARGE default / 7-symbol-prepared | p95 | +71.83% | +1.58% | 1.98% | 1.60% | 1 |

The SMALL default / 93-integer-prepared median is -1.37% and -1.24%, above its
1.15% A/A floor in both rounds; SMALL Release is -0.85% and -1.26%, with a 1.20%
floor (only the second round exceeds it). These limited observations do not
establish a general speedup or explain the other rows. In particular, the large
single-round symbol p95 remains unattributed. No timing is pooled across this
run and the release comparison, even though the reported CPU model matches.

A bounded read of every total-loop sample in the LARGE default / 7-symbol
comparison preserves the original p95 event: candidate round 1 has 201.539 us
p95 versus 112.851 us for #143. Fixed consecutive bins of 100 samples show the
candidate wall median around 100 us in bins 0--399, then 138.34 us in 400--499;
the matching thread-CPU median in that last bin is 139.96 us. That process has
no recorded GC event, page fault, involuntary switch or CPU change at request
boundaries. The next round has 117.911 us candidate p95. These observations
neither identify a hardware mechanism nor show that all CPU migrations were
absent; the original event and all samples remain unexplained and retained.

The separately dispatched [count follow-up 36574841013](https://github.com/maelys-dev/maelys-datalog/actions/runs/36574841013)
reuses these exact installed SDKs after verifying the report digest and every
selected binary hash. It counts six prospectively declared fixtures, two roles,
two reversed repetitions and five complete-request indices: 24 processes and
120 regions. Only the instrumentation helper is built; Valgrind latency is not
used. Its results are pending.
