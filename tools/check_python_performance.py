#!/usr/bin/env python3
# SPDX-License-Identifier: MPL-2.0
"""Locate Python performance evidence and select a published reference.

This optional read-only helper does not approve timings or gate a tag.
Maintainers record their release decision in the changelog pull request.
"""
import argparse
import json
import os
from pathlib import Path
import re
import subprocess
import sys
from urllib.request import Request, urlopen
from urllib.parse import urlencode

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "bench"))
from python_perf import revision

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
        raise ValueError(f"Python performance evidence is not available: {latest['html_url']} "
                         f"({latest.get('status')}/{latest.get('conclusion')})")
    return latest


def require_artifact(artifacts, commit):
    found = [a for a in artifacts if a.get("name") == ARTIFACT_PREFIX + commit and not a.get("expired", True)]
    if len(found) != 1:
        raise ValueError("Missing, expired or ambiguous Python performance artifact; obtain new evidence")
    return found[0]


def check(ref="HEAD"):
    commit = revision(ref)
    runs = api("actions/workflows/python-performance.yml/runs?" + urlencode({"head_sha": commit, "per_page": 100}))
    run = select_run(runs["workflow_runs"], commit)
    require_artifact(api(f"actions/runs/{run['id']}/artifacts?per_page=100")["artifacts"], commit)
    jobs = api(f"actions/runs/{run['id']}/jobs?filter=latest&per_page=100")["jobs"]
    if not any(job.get("name") == "Python performance evidence" and job.get("conclusion") == "success" for job in jobs):
        raise ValueError("The evidence job did not succeed (a skipped workflow is not evidence)")
    print(f"Python performance report available for {commit}: {run['html_url']}")
    print("Read the report and record the decision in the changelog PR; availability is not performance approval.")


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


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    sub = parser.add_subparsers(dest="command", required=True)
    p = sub.add_parser("check", help="Locate available evidence; does not approve performance")
    p.add_argument("--ref", default="HEAD", help="Measured commit or ref (default: HEAD)")
    sub.add_parser("previous-release")
    args = parser.parse_args()
    try:
        if args.command == "check":
            check(args.ref)
        else:
            previous_release()
    except (ValueError, TypeError, OSError, KeyError, subprocess.CalledProcessError) as error:
        print(f"Python performance evidence unavailable: {error}", file=sys.stderr)
        sys.exit(1)
