#!/usr/bin/env python3
# SPDX-License-Identifier: MPL-2.0
"""Require exact-commit Python performance evidence before a release.

This reads public Actions metadata without a release write token. Review is a
separate writer-dispatched run, bound to an existing report's SHA-256.
"""
import argparse
import json
import math
import os
from pathlib import Path
import re
import subprocess
import sys
from urllib.request import Request, urlopen
from urllib.parse import urlencode

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "bench"))
from python_perf import ANCHOR, CONFIGS, HARNESS_FILES, HISTORICAL, SCHEMA, historical_detected, revision, sha256
from python_workload import CASES, COLD_CASES, PHASES

REPOSITORY = "maelys-dev/maelys-datalog"
WORKFLOW = ".github/workflows/python-performance.yml"
ARTIFACT_PREFIX = "python-performance-"


def api(path):
    headers = {"Accept": "application/vnd.github+json", "X-GitHub-Api-Version": "2022-11-28"}
    if os.environ.get("GH_TOKEN"):
        headers["Authorization"] = "Bearer " + os.environ["GH_TOKEN"]
    request = Request(f"https://api.github.com/repos/{REPOSITORY}/{path}", headers=headers)
    with urlopen(request, timeout=30) as response:
        return json.load(response)


def valid_run(run, commit):
    return (run.get("head_sha") == commit and run.get("path") == WORKFLOW and
            run.get("repository", {}).get("full_name") == REPOSITORY and
            run.get("head_repository", {}).get("full_name") == REPOSITORY and
            run.get("event") in ("push", "workflow_dispatch"))


def select_run(runs, commit):
    candidates = [run for run in runs if valid_run(run, commit)]
    if not candidates:
        raise ValueError(f"No Python performance run on {commit}. Wait for the main push run or dispatch it.")
    # A failed/newer attempt must not be masked by a previously green run.
    latest = max(candidates, key=lambda run: (run.get("run_started_at") or run["created_at"], run["id"]))
    if latest.get("status") != "completed" or latest.get("conclusion") != "success":
        raise ValueError(f"Python performance evidence is not accepted: {latest['html_url']} "
                         f"({latest.get('status')}/{latest.get('conclusion')})")
    return latest


def require_artifact(artifacts, commit):
    found = [a for a in artifacts if a.get("name") == ARTIFACT_PREFIX + commit and not a.get("expired", True)]
    if len(found) != 1:
        raise ValueError("Missing, expired or ambiguous Python performance artifact; obtain new evidence")
    return found[0]


def check():
    commit = revision("HEAD")
    # A local source edit is not covered by HEAD's receipt. VERSION/changelog are
    # not excluded: the final bump/merge gets its own automatic measurement.
    if subprocess.check_output(["git", "status", "--porcelain"], cwd=ROOT).strip():
        raise ValueError("Commit tracked changes before checking exact-commit Python performance evidence")
    runs = api("actions/workflows/python-performance.yml/runs?" + urlencode({"head_sha": commit, "per_page": 100}))
    run = select_run(runs["workflow_runs"], commit)
    require_artifact(api(f"actions/runs/{run['id']}/artifacts?per_page=100")["artifacts"], commit)
    jobs = api(f"actions/runs/{run['id']}/jobs?filter=latest&per_page=100")["jobs"]
    if not any(job.get("name") == "Python performance evidence" and job.get("conclusion") == "success" for job in jobs):
        raise ValueError("The evidence job did not succeed (a skipped workflow is not evidence)")
    print(f"Python performance evidence accepted for {commit}: {run['html_url']}")


def previous_release():
    # Releases, not arbitrary local tags. A replay after publication excludes its
    # own commit. Both references are rebuilt by the same harness/toolchain.
    head = revision("HEAD")
    candidates = []
    for page in range(1, 11):
        releases = api(f"releases?per_page=100&page={page}")
        for release in releases:
            if release["draft"] or release["prerelease"]:
                continue
            tag = release["tag_name"]
            if not re.fullmatch(r"v[0-9]+\.[0-9]+\.[0-9]+", tag):
                continue
            commit = revision(tag)
            if commit != head and subprocess.run(["git", "merge-base", "--is-ancestor", commit, head], cwd=ROOT).returncode == 0:
                candidates.append((release["published_at"], commit))
        if len(releases) < 100:
            break
    if candidates:
        print(max(candidates)[1])
        return
    raise ValueError("No previous published ancestor release found")


def validate_review(report, commit, source_run, digest, path):
    if not re.fullmatch(r"[0-9a-f]{64}", digest) or sha256(path) != digest:
        raise ValueError("The report does not match the reviewed SHA-256")
    if (report.get("schema") != SCHEMA or report.get("status") != "review_required" or
            report.get("release_eligible") is not True or report.get("configs") != CONFIGS or
            report.get("commits", {}).get("head") != commit or
            report.get("commits", {}).get("anchor") != ANCHOR or
            report.get("commits", {}).get("historical") != HISTORICAL or
            report.get("environment", {}).get("run_id") != str(source_run)):
        raise ValueError("Review requires a complete hosted report on this exact commit, with timing observations only")
    if (report.get("samples") != {"warm": 501, "cold": 31} or not report.get("rows") or
            report.get("historical_control", {}).get("detected") is not True):
        raise ValueError("Incomplete evidence")
    if (set(report.get("harness", {})) != set(HARNESS_FILES) or
            any(sha256(ROOT / name) != digest for name, digest in report["harness"].items())):
        raise ValueError("The reviewed harness changed")
    scenarios = {f"{config}/{case}/{phase}" for config in CONFIGS for case in CASES for phase in PHASES}
    scenarios |= {f"{config}/{case}/cold" for config in CONFIGS for case in COLD_CASES}
    for rows, references in ((report["rows"], ("base", "anchor")),
                             (report["historical_control"].get("rows", []), ("anchor",))):
        metrics = {}
        for row in rows:
            key = row.get("reference"), row.get("scenario")
            seen = metrics.setdefault(key, set())
            metric = row.get("metric")
            if metric in seen or metric not in ("min", "median", "p95"):
                raise ValueError("Duplicate or unknown timing metric")
            seen.add(metric)
            floor, deltas = row.get("aa_floor"), row.get("delta", [])
            if (not isinstance(floor, (float, int)) or not math.isfinite(floor) or floor < 0 or
                    len(deltas) != 2 or any(not isinstance(d, (float, int)) or not math.isfinite(d) for d in deltas)):
                raise ValueError("Invalid timing observations")
            classes = ["slower" if d > floor else "faster" if d < -floor else "indeterminate" for d in deltas]
            if row.get("classification") != classes or row.get("review_required") != ("slower" in classes):
                raise ValueError("Timing classifications changed")
        if (set(metrics) != {(ref, key) for ref in references for key in scenarios} or
                any(value not in ({"min"}, {"median", "p95"}) for value in metrics.values())):
            raise ValueError("Incomplete timing matrix")
    if (not any(row["review_required"] for row in report["rows"]) or
            not historical_detected(report["historical_control"]["rows"], CONFIGS)):
        raise ValueError("Missing timing finding or failed historical control")


def review(args):
    commit = revision("HEAD")
    if not args.source_run.isdecimal() or not args.reason.strip():
        raise ValueError("Review needs a source run ID, exact report digest and written rationale")
    run = api(f"actions/runs/{args.source_run}")
    if not valid_run(run, commit) or run.get("status") != "completed" or run.get("conclusion") != "failure":
        raise ValueError("Source must be a completed measurement needing review on the same commit")
    require_artifact(api(f"actions/runs/{args.source_run}/artifacts?per_page=100")["artifacts"], commit)
    args.output.mkdir(parents=True, exist_ok=False)
    subprocess.run(["gh", "run", "download", args.source_run, "--repo", REPOSITORY,
                    "--name", ARTIFACT_PREFIX + commit, "--dir", str(args.output)], check=True)
    path = args.output / "report.json"
    report = json.loads(path.read_text())
    validate_review(report, commit, args.source_run, args.report_sha256, path)
    if (args.output / "acceptance.json").exists():
        raise ValueError("Review the original measurement, not a previous acceptance")
    acceptance = dict(schema=1, commit=commit, source_run=args.source_run,
                      report_sha256=args.report_sha256, reason=args.reason,
                      actor=os.environ["GITHUB_ACTOR"], run_id=os.environ["GITHUB_RUN_ID"])
    (args.output / "acceptance.json").write_text(json.dumps(acceptance, indent=2) + "\n")
    with (args.output / "report.md").open("a") as stream:
        stream.write(f"\nExplicit acceptance by {acceptance['actor']}, run {acceptance['run_id']}.\n\n{args.reason}\n")
    print(json.dumps(acceptance, indent=2))


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    sub = parser.add_subparsers(dest="command", required=True)
    sub.add_parser("check")
    sub.add_parser("previous-release")
    p = sub.add_parser("review")
    p.add_argument("--source-run", required=True)
    p.add_argument("--report-sha256", required=True)
    p.add_argument("--reason", required=True)
    p.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    try:
        if args.command == "check":
            check()
        elif args.command == "previous-release":
            previous_release()
        else:
            review(args)
    except (ValueError, TypeError, OSError, KeyError, subprocess.CalledProcessError) as error:
        print(f"Python performance gate refused: {error}", file=sys.stderr)
        sys.exit(1)
