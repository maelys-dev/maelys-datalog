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
variants, including the injected variant with its own A/A samples. Below
10 microseconds (determined from reference A/A medians), the
comparator uses minima. Otherwise median and nearest-rank p95 each retain
their own A/A floor, the maximum relative difference of the two pairs in both
revisions. Raw nanoseconds and every round remain available.

An above-floor slowdown in **either** A/B round of any phase, total or cold
metric against either reference produces `review_required` (harness exit 2).
The workflow preserves that status, report and classifications, and emits a
warning without failing the job. This prevents a timing observation from
becoming an indirect tag gate through the socle's check inspection. Tooling
errors and an inconclusive injected control still fail: they did not produce
usable evidence. A green workflow means the measurement completed, not that a
maintainer approved the performance. Reproducible complete-request slowdowns
require a release decision; phase timings inform diagnosis.
Below-floor differences remain `indeterminate`, never “no overhead”. A fully
quiet report says `no_slowdown_observed`, a statement limited to these cases
and this run. Every injected warm total's primary metric (median, or minimum
below 10 microseconds) must classify slower in both rounds of **all eight cases
in all four configurations**. Missing samples or a missing injection fail;
the historical comparison cannot rescue them. If detection is incomplete, the instrument
returns `inconclusive_control`, which cannot be accepted as a timing exception.

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
- complete-request findings, both rounds, phase diagnostics and control status;
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
Historical candidate slowdowns still require review (exit 2) wherever observed;
they are not assumed on every platform. A missing or inconclusive injected
control refuses the run (exit 1). Exit 0 means
no above-floor slowdown was observed, not proof of equivalence. `--smoke`
reduces sampling and configurations; local runs always have
`release_eligible: false`. Their success validates tooling only.

Keep the declared scenarios and references stable. A dependency, compiler,
Python version, sample scheme, baseline or acceptance-policy change is a
reviewable code change, with comparator/injection tests and the historical comparison
replayed. Do not calibrate a permissive time threshold from a noisy run. Add
real workload regressions to the corpus when found; no finite suite covers all
Python programs or all platforms.
