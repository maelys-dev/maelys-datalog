# SPDX-License-Identifier: MPL-2.0
"""Tooling tests and injected regressions, not hosted performance evidence."""
import copy
import json
from pathlib import Path
import sys
import tempfile
import unittest
from unittest.mock import patch
from contextlib import redirect_stdout
from io import StringIO
from types import SimpleNamespace

ROOT = Path(__file__).resolve().parents[2]
sys.path[:0] = [str(ROOT / "bench"), str(ROOT / "tools")]
import python_perf as perf
import check_python_performance as gate


def samples(value=20000):
    return {role: {name: {"SMALL-Release/7-symbol-solve/total": [value] * 100}
                   for _, name in perf.schedule([role])} for role in ("base", "anchor", "head")}


class ComparisonTests(unittest.TestCase):
    def test_identical_values_are_indeterminate_not_zero_cost(self):
        self.assertTrue(all(row["classification"] == ["indeterminate"] * 2
                            for row in perf.compare(samples())))

    def test_lifecycle_regression_and_tail_regression_are_not_averaged_away(self):
        for tail_only in (False, True):
            with self.subTest(tail_only=tail_only):
                data = samples()
                key = next(iter(data["head"]["ab0"]))
                data["head"]["ab0"][key] = [20000] * 94 + [40000] * 6 if tail_only else [40000] * 100
                rows = perf.compare(data)
                self.assertTrue(any(row["review_required"] for row in rows))
                if tail_only:
                    self.assertFalse(any(row["review_required"] for row in rows if row["metric"] == "median"))

    def test_floors_are_per_metric_and_include_candidate_aa(self):
        data = samples()
        key = next(iter(data["head"]["ab0"]))
        data["head"]["aa1"][key][-6:] = [24000] * 6
        data["head"]["ab0"][key] = [22000] * 100
        rows = perf.compare(data)
        self.assertTrue(all(row["aa_floor"] == 0 for row in rows if row["metric"] == "median"))
        self.assertTrue(all(abs(row["aa_floor"] - .2) < 1e-10 for row in rows if row["metric"] == "p95"))

    def test_sub_ten_microseconds_use_minimum(self):
        self.assertEqual({row["metric"] for row in perf.compare(samples(9000))}, {"min"})

    def test_incomplete_and_invalid_data_fail_closed(self):
        for mutation in ("pass", "case", "nan", "zero", "empty"):
            data = samples()
            key = next(iter(data["head"]["ab0"]))
            if mutation == "pass":
                del data["head"]["ab0"]
            elif mutation == "case":
                data["head"]["ab0"].clear()
            else:
                data["head"]["ab0"][key] = {"nan": [float("nan")], "zero": [0], "empty": []}[mutation]
            with self.subTest(mutation=mutation), self.assertRaises(ValueError):
                perf.compare(data)
        empty = samples()
        for passes in empty.values():
            for values in passes.values():
                values.clear()
        with self.assertRaises(ValueError):
            perf.compare(empty)

    def test_historical_control_must_detect_both_rounds_in_every_build(self):
        data = samples()
        key = next(iter(data["head"]["ab0"]))
        for i in range(2):
            data["head"][f"ab{i}"][key] = [35000] * 100
        rows = perf.compare(data)
        self.assertTrue(perf.historical_detected(rows, ["SMALL-Release"]))
        self.assertFalse(perf.historical_detected(rows, perf.CONFIGS))
        self.assertFalse(perf.historical_detected(perf.compare(samples()), ["SMALL-Release"]))

    def test_schedule_has_two_aa_pairs_then_alternates_all_revisions(self):
        plan = perf.schedule(["a", "b", "c"])
        self.assertEqual(plan[:4], [("a", f"aa{i}") for i in range(4)])
        self.assertEqual(plan[-6:], [(role, f"ab{i}") for i in range(2) for role in "abc"])


class EvidenceTests(unittest.TestCase):
    def test_previous_reference_is_published_ancestor_not_api_order_or_candidate(self):
        def release(tag, date, **extra):
            return dict(tag_name=tag, published_at=date, draft=False, prerelease=False, **extra)
        releases = [release("v1.0.0", "2026-01-01"), release("v1.0.2", "2026-03-01"),
                    release("v1.0.1", "2026-02-01"), release("v9.0.0", "2026-04-01")]
        def commit(ref):
            return "candidate" if ref in ("HEAD", "v1.0.2") else ref
        def ancestor(args, **kwargs):
            return SimpleNamespace(returncode=int("v9.0.0" in args))
        output = StringIO()
        with patch.object(gate, "api", return_value=releases), patch.object(gate, "revision", side_effect=commit), \
                patch.object(gate.subprocess, "run", side_effect=ancestor), redirect_stdout(output):
            gate.previous_release()
        self.assertEqual(output.getvalue().strip(), "v1.0.1")

    def run_record(self, **updates):
        run = dict(head_sha="a" * 40, path=gate.WORKFLOW,
                   repository=dict(full_name=gate.REPOSITORY),
                   head_repository=dict(full_name=gate.REPOSITORY), event="push",
                   id=10, created_at="2026-09-27T00:00:00Z", status="completed",
                   conclusion="success", html_url="https://example.invalid/run/10")
        run.update(updates)
        return run

    def test_missing_stale_other_workflow_and_fork_runs_are_not_evidence(self):
        for updates in ({"head_sha": "b" * 40}, {"path": ".github/workflows/ci.yml"},
                        {"event": "pull_request"}, {"head_repository": {"full_name": "someone/fork"}}):
            with self.subTest(updates=updates), self.assertRaises(ValueError):
                gate.select_run([self.run_record(**updates)], "a" * 40)
        with self.assertRaises(ValueError):
            gate.select_run([], "a" * 40)

    def test_latest_failure_or_pending_never_falls_back_to_old_green(self):
        for status, conclusion in (("completed", "failure"), ("in_progress", None), ("completed", "skipped")):
            later = self.run_record(id=11, status=status, conclusion=conclusion)
            with self.subTest(status=status, conclusion=conclusion), self.assertRaises(ValueError):
                gate.select_run([self.run_record(), later], "a" * 40)
        rerun = self.run_record(id=9, run_started_at="2026-09-28T00:00:00Z", conclusion="failure")
        with self.assertRaises(ValueError):
            gate.select_run([self.run_record(), rerun], "a" * 40)

    def test_missing_expired_or_ambiguous_artifact_is_refused(self):
        artifact = dict(name=gate.ARTIFACT_PREFIX + "a" * 40, expired=False)
        self.assertEqual(gate.require_artifact([artifact], "a" * 40), artifact)
        for artifacts in ([], [dict(artifact, expired=True)], [artifact, artifact]):
            with self.assertRaises(ValueError):
                gate.require_artifact(artifacts, "a" * 40)

    def test_review_is_bound_to_report_commit_harness_and_control(self):
        scenarios = {f"{config}/{case}/{phase}" for config in perf.CONFIGS
                     for case in perf.CASES for phase in perf.PHASES}
        scenarios |= {f"{config}/{case}/cold" for config in perf.CONFIGS for case in perf.COLD_CASES}
        rows = [dict(reference=ref, scenario=case, metric=metric, aa_floor=.01,
                     delta=[.5, .5], classification=["slower", "slower"], review_required=True)
                for ref in ("base", "anchor") for case in scenarios for metric in ("median", "p95")]
        report = dict(schema=perf.SCHEMA, status="review_required", release_eligible=True,
                      configs=perf.CONFIGS, commits=dict(head="a" * 40, anchor=perf.ANCHOR, historical=perf.HISTORICAL),
                      environment=dict(run_id="10"), samples=dict(warm=501, cold=31),
                      rows=rows, historical_control=dict(detected=True, rows=[r for r in rows if r["reference"] == "anchor"]),
                      harness={name: perf.sha256(ROOT / name) for name in (
                          "bench/python_perf.py", "bench/python_workload.py", "bench/python-perf-requirements.txt")})
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "report.json"
            path.write_text(json.dumps(report))
            digest = perf.sha256(path)
            gate.validate_review(report, "a" * 40, "10", digest, path)
            with self.assertRaises(ValueError):
                gate.validate_review(report, "a" * 40, "10", "0" * 64, path)
            for field, value in (("release_eligible", False), ("status", "inconclusive_control"),
                                 ("configs", ["SMALL-Release"]), ("historical_control", {"detected": False}),
                                 ("commits", {"head": "b" * 40, "anchor": perf.ANCHOR}),
                                 ("environment", {"run_id": "11"}), ("harness", {}),
                                 ("rows", rows[:-1]), ("rows", rows + rows[:1])):
                invalid = copy.deepcopy(report)
                invalid[field] = value
                path.write_text(json.dumps(invalid))
                with self.subTest(field=field), self.assertRaises(ValueError):
                    gate.validate_review(invalid, "a" * 40, "10", perf.sha256(path), path)


if __name__ == "__main__":
    unittest.main()
