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
and its fixed scoped x86 count follow-up are pending. This change is draft;
there is no maintainer timing exception, merge approval or release acceptance.
