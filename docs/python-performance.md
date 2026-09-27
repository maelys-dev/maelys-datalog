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
metric against either reference produces `review_required` and a failed check.
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

## Publication: evidence on the exact commit

`scripts/verify-release.sh` calls
`python3 tools/check_python_performance.py check` **before** `make check`.
The same hook runs before `cut` creates the version branch and on every release
build target before packaging. It requires a successful evidence job on the
exact clean HEAD, from this repository's Python workflow, plus its unexpired
artifact. Missing/pending/failed/skipped evidence and API failures stop the
gate. A more recent failed run cannot fall back to an older green run.

The release bump and merge change the SHA: a report on the feature branch or
pre-bump main cannot authorize publication. Wait for the automatic main run
on the **final merge commit** before `cut --tag`; the tag's build gate checks
it again. Evidence expires after 90 days; replaying a much older release that
carries this gate requires new evidence. No tag is moved.

To measure again, dispatch on the branch/tag that names the commit:

```sh
gh workflow run python-performance.yml --ref main
python3 tools/check_python_performance.py check
```

If a report says `review_required`, inspect its full matrix and raw samples,
the deterministic CI contracts, and the practical tradeoff. Fix a demonstrated
regression before release. A maintainer may instead explicitly accept a
measured tradeoff or an unresolved timing observation; document the user-facing
cost in the changelog when appropriate. Review is a new dispatch **on the same
commit**, naming the original run, its report's exact digest, and a rationale:

```sh
gh workflow run python-performance.yml --ref main \
  -f source_run=RUN_ID -f report_sha256=REPORT_SHA256 \
  -f reason='Concrete decision, affected scenarios, evidence and accepted limits'
```

This path downloads the existing artifact and validates its commit, harness,
complete measurement mode and historical control. It adds `acceptance.json`
with the actor, source run, report digest and rationale. It does not rerun the
timings, edit their classifications, raise a threshold or move the baseline.
Malformed, incomplete or local smoke reports cannot be accepted. A reviewed
timing result does not waive functional, allocation or other required CI checks.
Writer access authorizes the dispatch; the existing release-environment
review remains the final publication approval.

Artifacts include `report.json`, `report.md`, `samples.csv`, raw process outputs,
binaries/headers and build logs. They stay in Actions (or local scratch), never
in git, and generate no automatic PR comments. The release gate reads public
[Actions run metadata](https://docs.github.com/en/rest/actions/workflow-runs)
without a write token; the review job uses Actions read permission to download
the artifact. No new release workflow generation or protection change is needed.

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
