#!/usr/bin/env python3
# SPDX-License-Identifier: MPL-2.0
"""Build installed Python consumers, then compare full request lifecycles.

Exit 0: no review trigger; 2: review required; 1: invalid run.
Local smoke evidence is never release evidence. Raw results stay in --output.
"""
import argparse
import csv
import hashlib
import importlib.metadata
import json
import math
import os
from pathlib import Path
import platform
import shutil
import statistics
import subprocess
import sys
import tarfile

from python_workload import CASES, COLD_CASES, PHASES, POSITIVE_CONTROL_REQUESTS
from python_telemetry import (CALIBRATION_EVERY, CALIBRATION_NS, VERSION as TELEMETRY_VERSION,
                              validate as validate_telemetry)

ROOT = Path(__file__).resolve().parents[1]
ANCHOR = "0f247a7c81ec4a297f35ccee2bf007344f72ac7e"  # v0.11.1; not a moving tag.
HISTORICAL = "e2c357eae1f441774f6c54b13ac20124da79686e"  # v0.11.0, informative.
CONFIGS = [f"{profile}-{build}" for profile in ("SMALL", "LARGE")
           for build in ("default", "Release")]
SCHEMA = 4
HARNESS_FILES = ("bench/python_perf.py", "bench/python_workload.py", "bench/python_telemetry.py",
                 "bench/python-perf-requirements.txt")


def host_description():
    result = dict(kernel=platform.release(), processor=platform.processor(),
                  affinity=sorted(os.sched_getaffinity(0)) if hasattr(os, 'sched_getaffinity') else None,
                  runner_environment=os.environ.get('RUNNER_ENVIRONMENT'))
    if platform.system() == 'Linux':
        result['lscpu'] = subprocess.check_output(['lscpu', '--json'], text=True)
    return result


def telemetry_controls(out, builds, commits, count):
    """Bounded observer/storage controls; never replace the main comparisons."""
    directory = out / 'telemetry-controls'
    directory.mkdir()
    comparisons = {}
    for role in ('base', 'head'):
        commit = commits[role]
        samples = {mode: {name: {} for _, name in schedule([mode])}
                   for mode in ('growing', 'fixed', 'telemetry')}
        env = dict(os.environ, PYTHONPATH=str(builds[commit, 'SMALL-Release']),
                   PYTHONHASHSEED='0', PYTHONDONTWRITEBYTECODE='1')
        outputs = {}
        for mode, name in schedule(samples):
            flags = {'growing': [], 'fixed': ['--fixed-storage'], 'telemetry': ['--telemetry']}[mode]
            for case in ('7-integer-prepared', '93-integer-prepared'):
                value = json.loads(command([sys.executable, '-B', ROOT / 'bench/python_workload.py',
                                            case, '--samples', count, '--warmup', 50] + flags,
                                           cwd=out, env=env))
                (directory / f'{role}-{mode}-{name}-{case}.json').write_text(json.dumps(value) + '\n')
                if value['output_sha256'] != outputs.setdefault(case, value['output_sha256']):
                    raise ValueError('observer control output mismatch')
                if value['request_repetitions'] != 1:
                    raise ValueError('observer control repeated the request')
                if mode == 'telemetry':
                    validate_telemetry(value, count, 50)
                elif 'telemetry' in value:
                    raise ValueError('instrumented plain control')
                if value['sample_storage'] != ('growing' if mode == 'growing' else 'fixed'):
                    raise ValueError('incorrect control storage')
                samples[mode][name][f'SMALL-Release/{case}/total'] = value['samples']['total']
        comparisons[role] = dict(commit=commit, observer=compare(samples, 'telemetry', ('fixed',)),
                                 storage=compare(samples, 'fixed', ('growing',)))
    return comparisons


def sha256(path):
    return hashlib.sha256(Path(path).read_bytes()).hexdigest()


def command(args, **kwargs):
    return subprocess.check_output([str(a) for a in args], text=True, **kwargs).strip()


def revision(ref):
    return command(["git", "rev-parse", "--verify", f"{ref}^{{commit}}"], cwd=ROOT)


def schedule(roles, *, counterbalanced=False):
    # Two A/A pairs per binary before timing the alternating comparisons.
    return [(role, f"aa{i}") for role in roles for i in range(4)] + [
        (role, f"ab{i}") for i in range(2)
        for role in (list(reversed(roles)) if counterbalanced and i == 1 else roles)]


def statistic(values, metric):
    if not values or any(not isinstance(x, (int, float)) or not math.isfinite(x) or x <= 0 for x in values):
        raise ValueError("missing/nonpositive/nonfinite timing samples")
    if metric == "min":
        return min(values)
    if metric == "median":
        return statistics.median(values)
    return sorted(values)[math.ceil(.95 * len(values)) - 1]


def compare(samples, candidate="head", references=("base", "anchor")):
    """samples[role][pass][config/case/phase] -> positive nanoseconds."""
    keys = set(samples[candidate]["aa0"])
    if not keys:
        raise ValueError("empty scenario matrix")
    for passes in samples.values():
        if set(passes) != {f"aa{i}" for i in range(4)} | {"ab0", "ab1"}:
            raise ValueError("incomplete pass matrix")
        if any(set(values) != keys for values in passes.values()):
            raise ValueError("incomplete scenario matrix")
    rows = []
    for base in references:
        for key in sorted(keys):
            # Decide the statistic using reference A/A only, before A/B.
            reference = [x for i in range(4) for x in samples[base][f"aa{i}"][key]]
            metrics = ["min"] if statistic(reference, "median") < 10000 else ["median", "p95"]
            for metric in metrics:
                floor = max(abs(statistic(samples[role][f"aa{i+1}"][key], metric) /
                                statistic(samples[role][f"aa{i}"][key], metric) - 1)
                            for role in (base, candidate) for i in (0, 2))
                deltas = [statistic(samples[candidate][f"ab{i}"][key], metric) /
                          statistic(samples[base][f"ab{i}"][key], metric) - 1 for i in range(2)]
                classes = ["slower" if d > floor else "faster" if d < -floor else "indeterminate"
                           for d in deltas]
                # One anomalous round is enough for review: no averaging away p95.
                rows.append(dict(reference=base, scenario=key, metric=metric,
                                 aa_floor=floor, delta=deltas, classification=classes,
                                 review_required="slower" in classes))
    return rows


def historical_detected(rows, configs):
    # Retain the old historical diagnostic; it no longer decides validity.
    return all(any(row["scenario"] == f"{config}/7-symbol-solve/total" and
                       row["metric"] == "median" and
                       row["classification"] == ["slower", "slower"] for row in rows)
               for config in configs)


def positive_detected(rows, configs):
    # Every declared warm total must detect the injected work in both rounds.
    return bool(configs) and all(any(
        row["scenario"] == f"{config}/{case}/total" and
        row["metric"] in ("median", "min") and
        row["classification"] == ["slower", "slower"] for row in rows)
        for config in configs for case in CASES)


def measurement_variants(commits):
    # Build identity and sampling identity are separate. Equal references can
    # share observations; neither head nor null may alias those observations.
    variants = {role: (commit, 1, 'head' if role == 'head' else 'reference')
                for role, commit in commits.items()}
    variants["positive_control"] = (commits["anchor"], POSITIVE_CONTROL_REQUESTS, 'positive')
    for role in ('base', 'anchor'):
        variants[f'{role}_null'] = (commits[role], 1, 'null')
    return variants


def screen_with_null(rows, null_rows):
    """Add a release-review layer without changing raw A/A classifications.

    The per-row envelope is empirical, not a confidence bound or attribution.
    Cold first requests remain visible but cannot trigger review automatically.
    """
    key = lambda row: (row['reference'], row['scenario'], row['metric'])
    controls = {key(row): row for row in null_rows}
    if len(controls) != len(null_rows) or set(controls) != {key(row) for row in rows}:
        raise ValueError('incomplete or duplicate null-control matrix')
    screened = []
    for row in rows:
        control = controls[key(row)]
        band = max(control['aa_floor'], *(abs(delta) for delta in control['delta']))
        cold = row['scenario'].endswith('/cold')
        decisions = ['informative_cold' if cold else
                     'no_aa_slowdown' if classification != 'slower' else
                     'beyond_null' if delta > band else 'not_distinguished_from_null'
                     for delta, classification in zip(row['delta'], row['classification'])]
        screened.append(dict(row, aa_review_required=row['review_required'],
                             null_band=band, screening=decisions,
                             review_required='beyond_null' in decisions))
    return screened


def assess(samples, configs):
    null_rows = [row for reference in ('base', 'anchor')
                 for row in compare(samples, f'{reference}_null', (reference,))]
    rows = screen_with_null(compare(samples), null_rows)
    positive = compare(samples, "positive_control", ("anchor",))
    historical = compare(samples, "historical", ("anchor",))
    detected = positive_detected(positive, configs)
    status = ("inconclusive_control" if not detected else
              "review_required" if any(row["review_required"] for row in rows)
              else "no_review_required")
    return rows, positive, historical, null_rows, detected, status


def build(source, dest, config, compiler, log):
    profile, build_type = config.split("-")
    build_dir, sdk, consumer = dest / "build", dest / "sdk", dest / "python"
    env = dict(os.environ, CC=compiler)
    for name in ("CPATH", "C_INCLUDE_PATH", "CPLUS_INCLUDE_PATH", "LIBRARY_PATH",
                 "CFLAGS", "CPPFLAGS", "LDFLAGS", "PYTHONPATH"):
        env.pop(name, None)
    commands = [
        ["cmake", "-S", source, "-B", build_dir, "-DBUILD_TESTING=OFF",
         f"-DCMAKE_C_COMPILER={compiler}", "-DCMAKE_INSTALL_LIBDIR=lib",
         f"-DMAELYS_DATALOG_PROFILE_LARGE={'ON' if profile == 'LARGE' else 'OFF'}",
         f"-DCMAKE_BUILD_TYPE={'' if build_type == 'default' else build_type}"],
        ["cmake", "--build", build_dir, "--target", "maelys_datalog_shared", "--parallel", "4"],
        ["cmake", "--install", build_dir, "--prefix", sdk, "--component", "sdk"],
        ["cmake", "--install", build_dir, "--prefix", sdk, "--component", "sdk-shared"]]
    for cmd in commands:
        subprocess.run([str(a) for a in cmd], env=env, stdout=log, stderr=log, check=True)
    shutil.copytree(source / "bindings/python", consumer,
                    ignore=shutil.ignore_patterns("build", "__pycache__", "*.so", "*.dylib"))
    subprocess.run([sys.executable, str(consumer / "build_cffi.py"), "--sdk-prefix", str(sdk)],
                   env=env, stdout=log, stderr=log, check=True)
    files = [p for parent in (consumer / "maelys_datalog", sdk / "include", sdk / "lib")
             for p in parent.rglob("*") if p.is_file() and p.suffix != ".pyc"]
    hashes = {str(p.relative_to(dest)): sha256(p) for p in sorted(files)}
    return consumer, hashes


def run(args):
    out = args.output.resolve()
    out.mkdir(parents=True, exist_ok=False)
    commits = {"base": revision(args.base), "anchor": revision(ANCHOR),
               "head": revision(args.head), "historical": revision(HISTORICAL)}
    configs = ["SMALL-Release"] if args.smoke else CONFIGS
    host = host_description()
    builds, hashes = {}, {}
    sources = {}
    # git archive, never a dirty checkout or a reused extension.
    for commit in dict.fromkeys(commits.values()):
        source = out / commit / "source"
        source.mkdir(parents=True)
        archive = out / f"{commit}.tar"
        with archive.open("wb") as stream:
            subprocess.run(["git", "archive", commit], cwd=ROOT, stdout=stream, check=True)
        with tarfile.open(archive) as stream:
            stream.extractall(source, filter="data")
        archive.unlink()
        sources[commit] = source
        for config in configs:
            dest = out / commit / config
            dest.mkdir()
            with (dest / "build.log").open("w") as log:
                print(f"build {commit[:12]} {config}", flush=True)
                builds[commit, config], hashes[f"{commit}/{config}"] = build(
                    source, dest, config, args.compiler, log)
    # All builds finish before any measurement; no per-process path overrides.
    variants = measurement_variants(commits)
    samples = {role: {name: {} for _, name in schedule([role])} for role in variants}
    raw = out / "raw"
    raw.mkdir()
    outputs = {}
    unique = list(dict.fromkeys(commits.values()))
    plan = schedule(list(dict.fromkeys(variants.values())), counterbalanced=True)
    warm_samples, cold_samples = (31, 2) if args.smoke else (501, 31)
    for config in configs:
        for (commit, repetitions, identity), pass_name in plan:
            print(f"measure {config} {commit[:12]} {identity} requests={repetitions} {pass_name}", flush=True)
            roles = [role for role, variant in variants.items()
                     if variant == (commit, repetitions, identity)]
            env = dict(os.environ, PYTHONPATH=str(builds[commit, config]),
                       PYTHONHASHSEED="0", PYTHONDONTWRITEBYTECODE="1")
            for case in CASES:
                cold = []
                repeats = cold_samples if case in COLD_CASES else 1
                for repeat in range(repeats):
                    value = json.loads(command([
                        sys.executable, "-B", ROOT / "bench/python_workload.py", case,
                        "--samples", warm_samples if repeat == 0 else 0,
                        "--warmup", 50, "--telemetry"] + (["--positive-control"] if repetitions > 1 else []),
                        cwd=out, env=env))
                    if value["request_repetitions"] != repetitions:
                        raise ValueError("workload did not execute the requested control variant")
                    filename = f"{config}-{commit}-{identity}-requests{repetitions}-{pass_name}-{case}-{repeat}.json"
                    (raw / filename).write_text(json.dumps(value) + "\n")
                    validate_telemetry(value, warm_samples if repeat == 0 else 0, 50)
                    old = outputs.setdefault(case, value["output_sha256"])
                    if old != value["output_sha256"]:
                        raise ValueError(f"output mismatch {case}")
                    cold.append(value["cold"])
                    if repeat == 0:
                        for role in roles:
                            for phase in PHASES:
                                samples[role][pass_name][f"{config}/{case}/{phase}"] = value["samples"][phase]
                if case in COLD_CASES:
                    for role in roles:
                        samples[role][pass_name][f"{config}/{case}/cold"] = cold
    rows, positive, historical, null_rows, control_ok, status = assess(samples, configs)
    print('measure separate observer/storage controls', flush=True)
    observation_controls = telemetry_controls(out, builds, commits, warm_samples)
    report = dict(schema=SCHEMA, status=status,
                  release_eligible=bool(not args.smoke and os.environ.get("GITHUB_ACTIONS") == "true"
                                        and os.environ.get("GITHUB_SHA") == commits["head"]),
                  commits=commits, configs=configs, telemetry_controls=observation_controls,
                  telemetry=dict(version=TELEMETRY_VERSION, sample_storage='fixed', contemporaneous=True,
                                 calibration_every=CALIBRATION_EVERY, calibration_budget_ns=CALIBRATION_NS,
                                 filters_samples=False, corrects_latencies=False),
                  variants={role: dict(commit=sha, request_repetitions=repetitions,
                                       sampling_identity=identity)
                            for role, (sha, repetitions, identity) in variants.items()},
                  measurement_schedule=[dict(commit=sha, request_repetitions=repetitions,
                                             sampling_identity=identity, pass_name=name)
                                        for (sha, repetitions, identity), name in plan],
                  samples=dict(warm=warm_samples, cold=cold_samples),
                  environment=dict(python=sys.version, platform=platform.platform(), machine=platform.machine(), host=host,
                                   compiler=command([args.compiler, "--version"]),
                                   cmake=command(["cmake", "--version"]),
                                   packages={name: importlib.metadata.version(name) for name in ("cffi", "pycparser", "setuptools")},
                                   runner=os.environ.get("RUNNER_NAME"),
                                   run_id=os.environ.get("GITHUB_RUN_ID"),
                                   run_attempt=os.environ.get("GITHUB_RUN_ATTEMPT")),
                  harness={name: sha256(ROOT / name) for name in HARNESS_FILES},
                  binaries=hashes, outputs=outputs, rows=rows,
                  null_control=dict(rows=null_rows, references=['base', 'anchor'],
                                    same_binary_path=True, independent_samples=True,
                                    band='max(null aa_floor, abs(null delta round 1), abs(null delta round 2))',
                                    scope='per reference/scenario/metric; observed run only'),
                  cold_informative_only=True,
                  positive_control=dict(detected=control_ok, rows=positive,
                                        reference="anchor", request_repetitions=POSITIVE_CONTROL_REQUESTS),
                  historical_control=dict(detected=historical_detected(historical, configs),
                                          informative_only=True, rows=historical))
    report_path = out / "report.json"
    report_path.write_text(json.dumps(report, indent=2) + "\n")
    with (out / "samples.csv").open("w", newline="") as stream:
        writer = csv.writer(stream)
        writer.writerow(["role", "pass", "scenario", "sample", "ns"])
        for role, passes in samples.items():
            for name, cases in passes.items():
                for case, values in cases.items():
                    writer.writerows((role, name, case, i, value) for i, value in enumerate(values))
    summary = ["# Python performance evidence", "", f"Status: **{report['status']}**.",
               f"Candidate: `{commits['head']}`; previous: `{commits['base']}`; anchor: `{ANCHOR}`.",
               f"Report SHA-256: `{sha256(report_path)}`.", "",
               f"Injected {POSITIVE_CONTROL_REQUESTS}-request control detected in every case/configuration: **{control_ok}**.",
               "This coarse control does not certify sensitivity to smaller regressions.",
               f"Historical v0.11.0 quickstart slowdown detected everywhere (informative): **{report['historical_control']['detected']}**.", "",
               "A/A floors measure within-binary repeatability, not placement effects between binaries.",
               "Below-floor differences are indeterminate. Equal instructions would not establish equal cycles.",
               "Schema 4: warm above-floor slowdowns beyond their matched null envelope require review.",
               "The envelope is max(null A/A floor, absolute null gaps in both rounds), per reference/scenario/metric.",
               "It is an observed range, not a confidence bound, tolerance or proof of no regression.",
               "Cold first-request measurements are informative only; every raw classification is retained.",
               "A no_review_required status is not performance approval or causal attribution.", "",
               f"Candidate rows requiring review: **{sum(r['review_required'] for r in rows)}**; "
               f"warm rows not distinguished from null in at least one round: **{sum('not_distinguished_from_null' in r['screening'] for r in rows)}**.",
               f"Cold rows above their original A/A floor (informative): **{sum(r['aa_review_required'] and r['scenario'].endswith('/cold') for r in rows)}**.", "",
               "All measured requests carry contemporaneous CPU/resource/GC telemetry.",
               "Independent ~20 µs Python calibration every 32 samples is outside request timing.",
               "Preallocated sample/event storage and observation perturb execution; see separate controls below.",
               "No samples are filtered or corrected; separate phase and total loops cannot be paired by index.", "",
               "| Reference | Scenario | Metric | A/A % | Round 1 % | Round 2 % | Raw classes | Null band % | Review screening |",
               "|---|---|---|---:|---:|---:|---|---:|---|"]
    for title, section in (("Candidate", rows), ("Identical-binary null controls", null_rows),
                           ("Injected positive control", positive),
                           ("Historical comparison (informative)", historical)) + tuple(
            (f"{role}: {kind} control (separate, bounded diagnostic)", values[kind])
            for role, values in observation_controls.items() for kind in ('observer', 'storage')):
        if title != "Candidate":
            summary.extend(["", f"## {title}", "",
                            "| Reference | Scenario | Metric | A/A % | Round 1 % | Round 2 % | Raw classes | Null band % | Review screening |",
                            "|---|---|---|---:|---:|---:|---|---:|---|"])
        for row in section:
            band = f"{100*row['null_band']:.2f}" if 'null_band' in row else ''
            summary.append(f"| {row['reference']} | {row['scenario']} | {row['metric']} | "
                           f"{100*row['aa_floor']:.2f} | {100*row['delta'][0]:+.2f} | "
                           f"{100*row['delta'][1]:+.2f} | {', '.join(row['classification'])} | "
                           f"{band} | {', '.join(row.get('screening', []))} |")
    (out / "report.md").write_text("\n".join(summary) + "\n")
    # Preserve binaries, headers and build logs, not intermediate object trees.
    for commit in unique:
        shutil.rmtree(sources[commit])
        for config in configs:
            shutil.rmtree(out / commit / config / "build")
    print(f"{report['status']}: {report_path}; sha256={sha256(report_path)}", flush=True)
    return {"inconclusive_control": 1, "review_required": 2, "no_review_required": 0}[status]


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--head", default="HEAD")
    parser.add_argument("--base", required=True)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--compiler", default="clang")
    parser.add_argument("--smoke", action="store_true", help="short tooling test, never release evidence")
    sys.exit(run(parser.parse_args()))
