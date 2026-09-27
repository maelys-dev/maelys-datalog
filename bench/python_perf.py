#!/usr/bin/env python3
# SPDX-License-Identifier: MPL-2.0
"""Build installed Python consumers, then compare full request lifecycles.

Exit 0: no above-floor slowdown observed; 2: review required; 1: invalid run.
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

from python_workload import CASES, COLD_CASES, PHASES

ROOT = Path(__file__).resolve().parents[1]
ANCHOR = "0f247a7c81ec4a297f35ccee2bf007344f72ac7e"  # v0.11.1; not a moving tag.
HISTORICAL = "e2c357eae1f441774f6c54b13ac20124da79686e"  # v0.11.0 regression.
CONFIGS = [f"{profile}-{build}" for profile in ("SMALL", "LARGE")
           for build in ("default", "Release")]
SCHEMA = 1
HARNESS_FILES = ("bench/python_perf.py", "bench/python_workload.py", "bench/python-perf-requirements.txt")


def sha256(path):
    return hashlib.sha256(Path(path).read_bytes()).hexdigest()


def command(args, **kwargs):
    return subprocess.check_output([str(a) for a in args], text=True, **kwargs).strip()


def revision(ref):
    return command(["git", "rev-parse", "--verify", f"{ref}^{{commit}}"], cwd=ROOT)


def schedule(roles):
    # Two A/A pairs per binary before timing the alternating comparisons.
    return [(role, f"aa{i}") for role in roles for i in range(4)] + [
        (role, f"ab{i}") for i in range(2) for role in roles]


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
    # A named positive control, not a universal noise tolerance. Only the
    # complete warm quickstart median is the sentinel; other metrics stay raw.
    return all(any(row["scenario"] == f"{config}/7-symbol-solve/total" and
                       row["metric"] == "median" and
                       row["classification"] == ["slower", "slower"] for row in rows)
               for config in configs)


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
    files = [p for parent in (consumer / "maelys_datalog", sdk / "include")
             for p in parent.rglob("*") if p.is_file() and p.suffix != ".pyc"]
    hashes = {str(p.relative_to(dest)): sha256(p) for p in sorted(files)}
    return consumer, hashes


def run(args):
    out = args.output.resolve()
    out.mkdir(parents=True, exist_ok=False)
    commits = {"base": revision(args.base), "anchor": revision(ANCHOR),
               "head": revision(args.head), "historical": revision(HISTORICAL)}
    configs = ["SMALL-Release"] if args.smoke else CONFIGS
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
    samples = {role: {name: {} for _, name in schedule([role])} for role in commits}
    raw = out / "raw"
    raw.mkdir()
    outputs = {}
    unique = list(dict.fromkeys(commits.values()))
    plan = schedule(unique)
    warm_samples, cold_samples = (31, 2) if args.smoke else (501, 31)
    for config in configs:
        for commit, pass_name in plan:
            print(f"measure {config} {commit[:12]} {pass_name}", flush=True)
            roles = [role for role, sha in commits.items() if sha == commit]
            env = dict(os.environ, PYTHONPATH=str(builds[commit, config]),
                       PYTHONHASHSEED="0", PYTHONDONTWRITEBYTECODE="1")
            for case in CASES:
                cold = []
                repeats = cold_samples if case in COLD_CASES else 1
                for repeat in range(repeats):
                    value = json.loads(command([
                        sys.executable, "-B", ROOT / "bench/python_workload.py", case,
                        "--samples", warm_samples if repeat == 0 else 0,
                        "--warmup", 50], cwd=out, env=env))
                    filename = f"{config}-{commit}-{pass_name}-{case}-{repeat}.json"
                    (raw / filename).write_text(json.dumps(value) + "\n")
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
    rows = compare(samples)
    control = compare(samples, "historical", ("anchor",))
    control_ok = historical_detected(control, configs)
    needs_review = any(row["review_required"] for row in rows)
    status = ("inconclusive_control" if not control_ok else
              "review_required" if needs_review else "no_slowdown_observed")
    report = dict(schema=SCHEMA, status=status,
                  release_eligible=bool(not args.smoke and os.environ.get("GITHUB_ACTIONS") == "true"
                                        and os.environ.get("GITHUB_SHA") == commits["head"]),
                  commits=commits, configs=configs,
                  samples=dict(warm=warm_samples, cold=cold_samples),
                  environment=dict(python=sys.version, platform=platform.platform(), machine=platform.machine(),
                                   compiler=command([args.compiler, "--version"]),
                                   cmake=command(["cmake", "--version"]),
                                   packages={name: importlib.metadata.version(name) for name in ("cffi", "pycparser", "setuptools")},
                                   runner=os.environ.get("RUNNER_NAME"),
                                   run_id=os.environ.get("GITHUB_RUN_ID"),
                                   run_attempt=os.environ.get("GITHUB_RUN_ATTEMPT")),
                  harness={name: sha256(ROOT / name) for name in HARNESS_FILES},
                  binaries=hashes, outputs=outputs, rows=rows,
                  historical_control=dict(detected=control_ok, rows=control))
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
               f"Historical v0.11.0 quickstart regression detected in every configuration: **{control_ok}**.", "",
               "A/A floors measure within-binary repeatability, not placement effects between binaries.",
               "Below-floor differences are indeterminate. Equal instructions would not establish equal cycles.",
               "An above-floor slowdown requires review; this is not algorithmic attribution.", "",
               "| Reference | Scenario | Metric | A/A % | Round 1 % | Round 2 % | Classes |",
               "|---|---|---|---:|---:|---:|---|"]
    for row in rows:
        summary.append(f"| {row['reference']} | {row['scenario']} | {row['metric']} | "
                       f"{100*row['aa_floor']:.2f} | {100*row['delta'][0]:+.2f} | "
                       f"{100*row['delta'][1]:+.2f} | {', '.join(row['classification'])} |")
    (out / "report.md").write_text("\n".join(summary) + "\n")
    # Preserve binaries, headers and build logs, not intermediate object trees.
    for commit in unique:
        shutil.rmtree(sources[commit])
        for config in configs:
            shutil.rmtree(out / commit / config / "build")
    print(f"{report['status']}: {report_path}; sha256={sha256(report_path)}", flush=True)
    return 1 if not control_ok else 2 if needs_review else 0


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--head", default="HEAD")
    parser.add_argument("--base", required=True)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--compiler", default="clang")
    parser.add_argument("--smoke", action="store_true", help="short tooling test, never release evidence")
    sys.exit(run(parser.parse_args()))
