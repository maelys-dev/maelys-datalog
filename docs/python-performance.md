# Python performance contract

Performance is part of the Python consumer contract. A native solve benchmark
alone does not protect it: the 0.11.0 regression was visible in the complete
`Ruleset.solve()` lifecycle, including session construction and destruction.
0.11.1 reduced that cost by sharing immutable compiled policies, bounding the
session reservation and avoiding unnecessary initialization. Its changelog
retains the measured instruction tradeoff and unresolved timing observations.

This contract prevents an unexplained Python slowdown from silently passing
the release ceremony. It does **not** certify zero overhead for every program,
Python version or machine. There is no universal percentage tolerance.

## Pull requests: deterministic contracts

The existing SMALL/LARGE native allocation guard bounds session creation
allocations, reserved bytes and initialization bytes, checks that destruction
does not clear the entire result, and forbids engine allocations in the
prepared append/solve/query/result-release path. It remains part of `make check`.

The installed-SDK Python tests run on Linux and macOS in both profiles. In
addition to the default-constructor and explanation tests,
`tests/python/test_performance_contract.py` exercises repeated convenience and
prepared solves with 1 and 93 facts. It bounds native calls, balances session
and result releases, and rejects configuration/explanation preparation in the
default path. Native call counts are not Python allocation counts: Python
objects, CFFI buffers and strings still allocate.

`Python performance tooling` tests the comparator with injected total and p95
regressions, incomplete/invalid samples, and missing, stale or expired evidence.
These are tooling tests, not performance measurements.

## Main commits: complete consumer measurements

`.github/workflows/python-performance.yml` runs automatically for every push to
`main`, including the final release merge, and can be dispatched by a writer.
It uses a separate GitHub-hosted Ubuntu 24.04 job with read permissions only.
The native `bench-compare.yml` stays manual, outside PR and required checks.
No release job or release write token runs a benchmark.

The harness uses CPython 3.12.12, Clang 18 and the dependencies pinned in
`bench/python-perf-requirements.txt`. It records full compiler, CMake, Python
and package versions, host identity, harness hashes, public-header hashes,
native library and CFFI extension hashes. Every revision is built from a git
archive, installed into a fresh SDK prefix, and consumed by its own binding.
All builds finish before any timings begin. There are no concurrent builds,
affinity/priority adjustments or selected-out passes within the job.

References are the previous published ancestor release, the durable **v0.11.1
commit `0f247a7c81ec4a297f35ccee2bf007344f72ac7e`**, and the informative
historical comparison v0.11.0 at `e2c357eae1f441774f6c54b13ac20124da79686e`. The durable
reference prevents a succession of individually accepted changes from hiding
their accumulated cost. Equal reference commits reuse the same binary and
samples; they are not presented as independent repetitions.

Schema 4 adds a permanent **identical-binary null control for each reference**
(`base_null`, `anchor_null`). Each loads the very same installed files, at the
same path, as its reference: no rebuild, copy-induced path difference or relink.
Fresh processes collect independent A/A and comparison samples with identical
workload and telemetry. Null roles may share samples with each other only when
their reference commits coincide; they never reuse ordinary reference samples.
The candidate is independently sampled even when its commit equals a reference.
`variants` records the commit, request count and sampling identity; raw filenames
include that identity, and `measurement_schedule` records the exact pass order.

Sensitivity is checked independently of that history: a benchmark-only wrapper
executes **three complete requests instead of one**, with the same installed
v0.11.1 binding and native binary. This variant shares the binary, not the
normal anchor's samples. No injected code or option is added to the distributed
binding, engine or SDK. Each repeated request performs input, solve, queries
and release; every answer from all three requests is checked after timing.
The ordinary transaction callable is unchanged. The injected control does not
use sleep, a busy-wait, a timer-derived delay or calibration to the ongoing run.

The fixed three-request choice adds two requests of real work (nominally 200%).
It was declared before the new run: this is 2.46 times the largest A/A floor
in the [first hosted report](https://github.com/maelys-dev/maelys-datalog/actions/runs/36305554306)
(81.19%, on a phase p95). This fixes work, not elapsed time; actual control
deltas and floors are measured and retained. It establishes detection of this
coarse cost only, not sensitivity to smaller regressions or validity of every
candidate speedup. The candidate's classifications and human review remain
separate. The original report stays `inconclusive_control` under its original
protocol and is not retrospectively validated by this change.

For each of SMALL/LARGE and default CMake/Release (`-O3`), all declared cases
run: 7 or 93 input facts, symbols or integers, `Ruleset.solve()` or a reused
prepared session. Input uses the public `add_fact` loop, including conversions;
the bulk-input API has functional and deterministic tests but is not timed here.
The warm request includes input construction/reset, solve, positive and negative
queries, result release and, for convenience calls, EDB/session destruction.
Policy compilation, prepared session creation and final prepared teardown are
outside the reused request. Separate diagnostic transactions time input,
solve, query and close; total request samples have no intermediate clocks.
Every transaction's answers are checked outside its timed region.

There are 501 warm samples after 50 warmups per case/pass. Cold request samples
are the first request in each of 31 fresh interpreters for the two
7-symbol cases. Import and policy compilation are outside that interval:
“cold” is first-request latency, not interpreter startup or a cold OS cache.
Other scenarios have one recorded first request but no cold-distribution claim.
The injected variant uses the same sample counts and cases. Its cold sample is
a group whose first request is cold and whose next two are warm; it is not
three cold starts. Its phase diagnostics sum the three per-request durations,
while its total includes the wrapper and all three complete requests.

Two A/A pairs per distinct binary/variant precede two interleaved rounds of all
variants, including the injected and null variants with their own A/A samples.
Schema 4 reverses the complete variant order in the second comparison round,
balancing each variant's position across the two rounds. Below
10 microseconds (determined from reference A/A medians), the
comparator uses minima. Otherwise median and nearest-rank p95 each retain
their own A/A floor, the maximum relative difference of the two pairs in both
revisions. Raw nanoseconds and every round remain available.

### Null controls and release-review screening (schema 4)

The raw comparator is unchanged: `aa_floor`, both `delta` values and both
`classification` values retain the original A/A interpretation for every row.
`aa_review_required` preserves the old candidate trigger, including cold rows.
No samples, signs or classifications are removed or recalculated with a wider
A/A floor. Schema-2/3 reports retain their original statuses and interpretation.

For each **reference / configuration / case / phase / statistic** separately,
the observed null envelope is declared as
`max(null A/A floor, abs(null round 1 delta), abs(null round 2 delta))`.
Both signs count because the two labels execute the same binary. The rule is
fixed before measuring; it never pools noise from unrelated cases, references
or statistics. This is a descriptive envelope of this run, **not a confidence
bound, universal tolerance or causal test**. Two rounds do not estimate a tail
bound on future process variation, and a noisy control can leave a real small
regression unresolved. A quiet null also cannot establish a software cause.

A **warm** raw `slower` classification beyond that row's null envelope in
**either** round produces `review_required` (harness exit 2). A raw slowdown
within the envelope stays visible as `not_distinguished_from_null`; it is not
reported as a candidate effect, nor as proof of equivalence. A larger candidate
gap still triggers review even when the null itself alerts. First-request
(`cold`) measurements are **informative only**, including their raw A/A alerts:
they do not trigger this automatic review status, but remain available for a
maintainer's investigation. Cold performance can still regress; this policy
change does not establish that every cold slowdown is environmental.

The workflow preserves that status, report and classifications, and emits a
warning without failing the job. This prevents a timing observation from
becoming an indirect tag gate through the socle's check inspection. Tooling
errors, missing/incomplete null samples and an inconclusive injected control
still fail: they did not produce usable evidence. A green workflow means the
measurement completed, not that a
maintainer approved the performance. Reproducible complete-request slowdowns
require a release decision; phase timings inform diagnosis.
Below-floor differences remain `indeterminate`, never “no overhead”. A report
without a warm trigger says `no_review_required`, **not** `no_slowdown_observed`:
it may contain unresolved warm observations and cold alerts. Review screening
does not approve the release. Every injected warm total's primary metric (median, or minimum
below 10 microseconds) must classify slower in both rounds of **all eight cases
in all four configurations**. Missing samples or a missing injection fail;
the historical comparison cannot rescue them. If detection is incomplete, the instrument
returns `inconclusive_control`, which cannot be accepted as a timing exception.
Positive-control validity uses its raw A/A classifications, never null screening.
The three-request positive control validates coarse sensitivity; the independent
null measures variation without a software change. Neither certifies the other.

The historical v0.11.0 comparison remains complete and informative. The first
hosted run did not detect its quickstart slowdown in SMALL-Release and only
detected one SMALL-default round. A regression observed on macOS was not an
established positive control for every Linux configuration. Schema 2 records
`positive_control` separately from `historical_control` (with
`informative_only: true`), preserves both full comparison tables and records
each role's binary commit and request count in `variants`. Neither the candidate
references nor its per-metric A/A algorithm changed.

### Contemporaneous telemetry (schema 3)

The complete benchmark now invokes `python_workload.py --telemetry` for **every**
variant/pass/configuration, including cold requests, warmups and the injected
control. The request's reported elapsed time and its `start_ns`/`end_ns` use
the **same clock reads**. Thread CPU timestamps enclose that interval; CPU-id
and `getrusage` snapshots enclose the CPU reads. Resource deltas include voluntary
and involuntary switches and minor/major faults. Linux uses `RUSAGE_THREAD`;
other systems explicitly report the process fallback. Linux CPU ids come from
libc `sched_getcpu` when Python has no wrapper (including CPython 3.12); `-1`
means unavailable/failed, never an inferred CPU id. Both endpoints can be equal
despite an intervening migration. Guest counters do not expose every host event.

GC callbacks record collection start/end timestamps, generation, collected and
uncollectable counts on the same monotonic clock. A nearby UTC anchor permits
approximate correlation with external logs; it does not turn the monotonic
clock into UTC or certify exact synchronization. Only intervals overlapping the
request can describe that request. Records name `cold`, `warmup`, `total` or
`phases` and their own sample index: **total and phase loops remain separate**.
This does not identify which phase caused a slow total request.

A separate pure-Python throughput probe runs before each group of 32 total or
phase samples and after the final sample, outside all measured requests. Its
fixed wall budget is **20,000 ns**, checked between blocks of 32 integer-loop
iterations. It retains iteration count, actual wall/thread duration, CPU ids and
resource deltas; timer checks and loop overhead are part of the probe. It can
overshoot or do zero work when descheduled. The budget is not tuned to noise or
used to inject the positive control. A lower nearby throughput supports common
pressure, but does not identify SMT/frequency or rule out in-process effects.
A stable probe cannot exclude a disturbance between probes, in native code or
in memory. No probe normalizes timings, removes samples or changes a floor.

Latency lists and numeric request/GC/calibration buffers are reserved before
the first request. GC-buffer overflow or incomplete/mislabelled telemetry makes
the run invalid, rather than silently losing evidence. Python objects, clock
reads, callbacks and recording still have costs. CPU time can slightly exceed
request wall time because its interval is wider; retain that difference.
The observer and between-request probes can change cache state, specialization
and collection timing even though their bookkeeping is outside request clocks.

After the complete matrix, a bounded observer/storage control uses the same
installed base/head binaries, SMALL-Release, 7/93-integer-prepared, 50 warmups
and 501 samples. Two A/A pairs precede two alternating rounds of three modes:
original growing lists without telemetry, fixed lists without telemetry, and
fixed lists with telemetry. All 72 process records remain in
`telemetry-controls/`; report tables separately compare fixed/growing storage
and telemetry/fixed storage. These observations neither change candidate status
nor establish a universal observer overhead. They explicitly retain the list
growth hypothesis and the observer's perturbation for review.

Schema 3 retains the same cases, references, positive-control work and comparator
as schema 2, but changes storage and adds observation. **Its latencies are not
interchangeable with historical uninstrumented latencies.** Every reference gets
the same new protocol and fresh A/A measurements. Historical artifacts and
classifications remain unchanged; none can retrospectively acquire telemetry.
CPU topology (`lscpu`), kernel, current affinity and runner environment are recorded
without changing placement or priority. A controlled ARM64 execution requires
an actually registered self-hosted runner and a writer-only dispatch; record its
identity and compare revisions on that machine, never raw x86/ARM latencies.
Self-hosting alone does not establish isolation or provide hardware counters.

An A/A floor describes one binary's repeatability, not systematic differences
between binaries. Timing observations alone do not identify an algorithm,
cache or layout mechanism. Instruction counts remain a separate diagnostic;
Callgrind Ir is not hardware retired instructions, and equal counts do not
establish equal cycles. Do not repeat the closed padding/layout investigations
merely to obtain a green Python report.

## Before the cut: human release review

### Bounded tail diagnostic

The optional `tail_diagnostic` dispatch runs a separate investigation of the
SMALL-Release / 7-integer-prepared p95 observation in run 36308020794. It compares
fixed v0.11.1 (`0f247a7`) with the measured candidate (`f975be6`), building both
before sampling. Four cases are declared in `bench/python_tail_diagnostic.py`:
the observed case, its symbol and convenience counterparts, and 93 integer
facts in a prepared session. Two A/A pairs precede twelve alternating comparisons
with reversed revision order in every other pair. Case and mode order rotate;
every process has 501 warm samples, 50 warmups and a checked first request.

`plain` invokes the workload without schema-3 telemetry in a fresh interpreter. `observe`
wraps its same transactions to collect elapsed and thread CPU time, getrusage
context-switch/page-fault deltas and GC callback intervals. Numeric sample
storage is preallocated. `gc-off` repeats the observer with cyclic collection
disabled; ordinary reference counting and Python/CFFI allocation remain.
Observer imports, calls and callbacks perturb execution and GC scheduling:
their latencies are diagnostic, not replacements for `plain` or release timing.

CPU clock reads surround the inner wall interval, so small negative wall-minus-CPU
differences are retained. Resource snapshots surround both clocks and can include
boundary work. A wall/CPU gap can support descheduling; equal increases cannot
distinguish more executed work from frequency, cache or host effects. GC intervals
are intersected with the same measured transaction; release phase samples remain
separate transactions and must not be correlated by index with total samples.
The fixed 200-microsecond tail listing is descriptive only: no samples are removed,
no A/A floor changes, and no old observation is reclassified. The report always
has `release_eligible: false` and uses a separate artifact name. This bounded run
does not authorize a release or reopen the closed padding/layout investigation.

The original schema-2 diagnostics retain their archived harness hashes. Later
changes to the workload driver must not be described as byte-identical replays
of those earlier runs. Concurrent CPU/wall inflation and a noisy or multimodal
tail are compatible with host pressure, but neither prove it nor formally exclude
the engine/binding. GC pauses need not have a fixed additive duration.

Clock and event semantics follow the Python 3.12 documentation for
[thread time](https://docs.python.org/3.12/library/time.html#time.thread_time),
[resource usage](https://docs.python.org/3.12/library/resource.html#resource.getrusage)
and [GC callbacks](https://docs.python.org/3.12/library/gc.html#gc.callbacks).

The separate `instruction_diagnostic` dispatch follows up observed CPU-time
bursts using the **original run 36308020794 SDK binaries**, verified against
that report and its file hashes. It does not rebuild the measured libraries.
Callgrind collection surrounds 16 predeclared complete transactions between
indices 0 and 500 for 7/93-integer-prepared, with two processes per revision/case
and opposite revision order on repetition. Raw profiles, per-function annotations,
Ir/Dr/Dw totals and checked answers are retained. The small client-request helper
is test instrumentation only; no native/binding API changes. These software
instruction counts cannot explain original cycles retrospectively or establish
hardware frequency/cache/host scheduling behavior. No Valgrind latency is reported.
Choose only one diagnostic input per dispatch; neither can approve a release.

### Release decision

Before `maelys-release cut ... --apply`, read the complete report and record
the following in the changelog pull request:

- measured commit and any subsequent changes included in the release;
- measurement run URL and the report's SHA-256;
- complete-request findings, both rounds, phase diagnostics and positive-control status;
- matched null envelopes, remaining warm triggers, unresolved warm observations
  and informative cold findings (schema 4);
- deterministic allocation/call-budget results and instruction evidence when relevant;
- the maintainer's decision and rationale, including unresolved observations
  and any accepted user-facing cost.

Fix a demonstrated regression or obtain the maintainer's explicit decision on
the concrete tradeoff before release. Missing or incomplete evidence is not a
pass: obtain a usable report before making that decision. Do not change
thresholds, remove cases or move references to obtain a quiet report. Preserve
the original report and classifications even when a tradeoff is accepted.
Document user-facing costs in the changelog when appropriate. Agents must not
infer acceptance of a finding from authorization to implement the benchmark.

The report identifies exactly what was measured. New runtime, binding, build
or harness changes require fresh measurements and an updated decision.
Version/documentation-only changes do not by themselves require another run;
record that scope in the changelog PR instead of demanding the final merge SHA
for every report. Artifacts are retained for 90 days: record the review while
they are available. Their later expiry must not prevent replay of an already
published tag.

There is no Python timing or Actions-artifact hook in `verify-release.sh`,
packaging or tag replay. The release verification script runs `make check`.
The generated release workflow and release-environment approval are unchanged.

To measure again, dispatch on the branch/tag that names the candidate. The
optional metadata check locates an available report on a named commit; it does
not read the timing findings or approve a release:

```sh
gh workflow run python-performance.yml --ref main
python3 tools/check_python_performance.py check --ref MEASURED_COMMIT
```

Artifacts include `report.json`, `report.md`, `samples.csv`, raw process outputs,
binaries/headers and build logs. They stay in Actions (or local scratch), never
in git, and generate no automatic PR comments. The optional locator reads public
[Actions run metadata](https://docs.github.com/en/rest/actions/workflow-runs)
without a write token. The release decision belongs in the changelog PR, not in
an acceptance dispatch. Do not make the timing workflow a required check.

## Local validation and maintaining the harness

Use a disposable virtual environment and output directory:

```sh
python3 -m venv /tmp/maelys-python-perf-env
/tmp/maelys-python-perf-env/bin/pip install -r bench/python-perf-requirements.txt
/tmp/maelys-python-perf-env/bin/python bench/python_perf.py \
  --base v0.11.1 --head v0.11.0 --smoke --output /tmp/python-negative-control
python3 -m unittest discover -s tests/tooling -p 'test_python_perf*.py' -v
```

The injected variant must be detected even when the historical pair is quiet.
Historical candidate warm slowdowns beyond their matched null envelope still
require review (exit 2);
they are not assumed on every platform. A missing or inconclusive injected
control refuses the run (exit 1). Exit 0 means no warm review trigger remains
under this protocol, not proof of equivalence or absence of raw alerts. `--smoke`
reduces sampling and configurations; local runs always have
`release_eligible: false`. Their success validates tooling only.

Keep the declared scenarios and references stable. A dependency, compiler,
Python version, sample scheme, baseline or acceptance-policy change is a
reviewable code change, with comparator/injection tests and the historical comparison
replayed. Do not calibrate a permissive time threshold from a noisy run. Add
real workload regressions to the corpus when found; no finite suite covers all
Python programs or all platforms.

### v0.13 release attribution follow-up

The writer-only `release_diagnostic` dispatch investigates the accepted
observations from run **36331650860**, report SHA-256
`57ad969645ca156262f798346267a077f65e8467a62c3c0e2bdb12b67573184a`.
`bench/python_release_instructions.py` verifies the original workload and every
retained consumer/header/library hash before using the actual measured binaries;
it never substitutes a rebuild. The four declared fixtures are SMALL-default /
7-integer-prepared, LARGE-default / 7-symbol-prepared, LARGE-Release /
7-symbol-prepared, and LARGE-Release / 93-integer-prepared. Base v0.12.0 and the
measured candidate run in each; the cold Release case also retains v0.11.1,
the reference for its p95 observation.

Two processes per revision/fixture, in opposite revision order, count the first
request and total samples 0, 100, 421, 422, 436, 448, 449 and 500. The original
501 samples, 50 warmups, schema-3 telemetry and checked answers remain. Collection
covers the complete transaction, excluding setup, clocks and answer checks;
helper/Python call-boundary instructions remain in the profile. Preserve raw
Callgrind profiles and exclusive per-function Ir/Dr/Dw annotations. No instrumented
latencies or simulated cache misses are interpreted as hardware measurements.
Separate `LD_DEBUG=bindings` traces mark the first transaction and record actual
Python `dlopen` flags, to test whether lazy symbol resolution occurs inside it.

This investigation does not alter the release report, its classifications, the
accepted tradeoff or the release gate. It does not reopen the closed native
padding sweeps. Equal software counts can exclude added executed work within
the counted samples; they cannot establish equal cycles or explain an earlier
transient retroactively. A hardware/runner cause still requires direct evidence.

The separate `release_process_control` dispatch uses those four fixtures and
the same original binaries. `base` and `base_copy` are **the same installed
consumer path and bytes**, executed in independent processes; `head` is the
original candidate. Four A/A passes per label precede twelve rounds containing
each of the six revision/label permutations twice. Each warm process keeps
501 requests, 50 warmups and schema-3 telemetry. Each symbol case retains 31
fresh-interpreter first requests per pass. All phase diagnostics, samples and
outputs remain in artifacts. Counts and timings run in separate jobs; no
Valgrind timing is used. The new diagnostic's A/A classifications and round
signs are preserved for both the identical-binary control and candidate.
They are not substituted for the published release matrix or used to widen
its thresholds. A same-binary label difference measures process/environment
variation in this run; it does not establish the cause of an earlier event.

#### Findings from the v0.13 attribution runs

[Instruction run 36338645720](https://github.com/maelys-dev/maelys-datalog/actions/runs/36338645720)
retains 18 processes and 162 scoped profiles. In the 72 base/candidate paired
scopes, exclusive Ir/Dr/Dw match for every reported native-engine and generated
CFFI-module function. Whole-process profiles match in 50/72 comparisons. All
remaining differences are localized: cold calls have -137 Ir/-22 Dr in the
unoptimized configurations or +122 Ir/+20 Dr in Release, solely in CPython
`_Py_dict_lookup` and `insertdict`; the call graph traces them to CFFI's
`get_or_insert_unique_type`. Warm differences are confined to libc
`__strcmp_avx2` (+/-72 Ir, unchanged Dr/Dw). These are scoped software counts,
not a cycle bound or a claim about every input.

[CFFI 2.0.0's type cache](https://github.com/python-cffi/cffi/blob/v2.0.0/src/c/_cffi_backend.c#L4730)
uses byte-string keys made from native addresses. Fixing `PYTHONHASHSEED` does
not fix those addresses or their dictionary probe paths. This identifies the
small executed-work difference at cold entry; it does not attribute the
original percentage latency gaps to that difference. All five separate loader
traces report RTLD_NOW and no symbol binding between the first-request markers:
lazy dynamic linking is not the cold-entry cause in those traces.

The original linked CFFI modules have the same 162 function bodies and relative
addresses in every configuration. The native library's PLT grows by three net
16-byte entries in default builds and one in Release: configured window
initializers add entries, while the windows' old call to input_edb_add_fact
vanishes. Native hot text therefore moves by 48 or 16 bytes. Data sections also
move. This establishes a layout change, not its hardware performance effect;
object-file equivalence alone misses it.

[Process-control run 36338922896](https://github.com/maelys-dev/maelys-datalog/actions/runs/36338922896)
keeps all twelve rounds. For SMALL-default/7-integer-prepared, the median of
round median deltas is +0.27% for head/base and +0.28% for base_copy/base. The
identical-binary control is classified slower in 6/12 LARGE-Release cold median
comparisons; its LARGE-default cold median gap reaches +9.10% in one round.
Head/base's median cold delta in LARGE-default is +0.19% across rounds. These
statistics describe this diagnostic, not a replacement release verdict.

The unchanged v0.12.0 binary also produces warm bursts: SMALL-default base aa3
has median 82.545 microseconds and p95 159.529 microseconds; base_copy aa0 has
median 81.783 and p95 160.671. Adjacent calibration drops from 480 to 320
iterations during those bursts. Requests above 1.5 times their own process
median (a descriptive listing, never an exclusion rule) show no overlapping GC,
page faults or recorded context switches in these two processes. Thus a v0.13
change is not necessary for this symptom; reduced nearby Python throughput
supports common execution pressure. It does not distinguish SMT, frequency,
interrupts, hypervisor behavior or in-process/cache effects.

Preserve the original accepted observations. This investigation found no added
engine work in its declared cases and demonstrated above-floor alerts without
any binary change. It does not establish an exact retrospective hardware cause
or authorize a timing-only engine fix. Future attribution at this scale should
include an independently measured identical-binary control and multiple
counterbalanced processes, alongside instruction evidence. Neither the control
nor a quiet later pass may erase a historical signal or widen its A/A floor.
