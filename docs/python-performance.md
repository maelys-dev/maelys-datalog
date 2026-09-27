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
commit `0f247a7c81ec4a297f35ccee2bf007344f72ac7e`**, and a positive detection
control: v0.11.0 at `e2c357eae1f441774f6c54b13ac20124da79686e`. The durable
reference prevents a succession of individually accepted changes from hiding
their accumulated cost. Equal reference commits reuse the same binary and
samples; they are not presented as independent repetitions.

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

Two A/A pairs per binary precede two interleaved rounds of all distinct
revisions. Below 10 microseconds (determined from reference A/A medians), the
comparator uses minima. Otherwise median and nearest-rank p95 each retain
their own A/A floor, the maximum relative difference of the two pairs in both
revisions. Raw nanoseconds and every round remain available.

An above-floor slowdown in **either** A/B round of any phase, total or cold
metric against either reference produces `review_required` (harness exit 2).
The workflow preserves that status, report and classifications, and emits a
warning without failing the job. This prevents a timing observation from
becoming an indirect tag gate through the socle's check inspection. Tooling
errors and an inconclusive historical control still fail: they did not produce
usable evidence. A green workflow means the measurement completed, not that a
maintainer approved the performance. Reproducible complete-request slowdowns
require a release decision; phase timings inform diagnosis.
Below-floor differences remain `indeterminate`, never “no overhead”. A fully
quiet report says `no_slowdown_observed`, a statement limited to these cases
and this run. The known 0.11.0 warm quickstart total-median regression must be
detected in both rounds of every configuration. If it is not, the instrument
returns `inconclusive_control`, which cannot be accepted as a timing exception.
That named control is not a noise tolerance for unrelated workloads.

An A/A floor describes one binary's repeatability, not systematic differences
between binaries. Timing observations alone do not identify an algorithm,
cache or layout mechanism. Instruction counts remain a separate diagnostic;
Callgrind Ir is not hardware retired instructions, and equal counts do not
establish equal cycles. Do not repeat the closed padding/layout investigations
merely to obtain a green Python report.

## Before the cut: human release review

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

The historical negative candidate should require review (exit 2), or refuse
an inconclusive run (exit 1), never establish release acceptance. Exit 0 means
no above-floor slowdown was observed, not proof of equivalence. `--smoke`
reduces sampling and configurations; local runs always have
`release_eligible: false`. Their success validates tooling only.

Keep the declared scenarios and references stable. A dependency, compiler,
Python version, sample scheme, baseline or acceptance-policy change is a
reviewable code change, with comparator tests and the real historical control
replayed. Do not calibrate a permissive time threshold from a noisy run. Add
real workload regressions to the corpus when found; no finite suite covers all
Python programs or all platforms.
