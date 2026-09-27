# SPDX-License-Identifier: MPL-2.0
"""Tooling tests and injected regressions, not hosted performance evidence."""
from pathlib import Path
import json
import sys
import os
import subprocess
import textwrap
import tempfile
import tarfile
import unittest
from unittest.mock import patch
from contextlib import redirect_stdout
from io import StringIO
from types import SimpleNamespace

ROOT = Path(__file__).resolve().parents[2]
sys.path[:0] = [str(ROOT / "bench"), str(ROOT / "tools")]
import python_perf as perf
import python_workload as workload
import check_python_performance as gate


def samples(value=20000):
    return {role: {name: {"SMALL-Release/7-symbol-solve/total": [value] * 100}
                   for _, name in perf.schedule([role])} for role in ("base", "anchor", "head")}


def controlled_samples():
    return {role: {name: {f"{config}/{case}/total": [value] * 100
                         for config in perf.CONFIGS for case in perf.CASES}
                   for _, name in perf.schedule([role])}
            for role, value in (("base", 20000), ("anchor", 20000), ("head", 20000),
                                ("historical", 20000), ("positive_control", 60000))}


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

    def test_historical_diagnostic_retains_its_original_classification(self):
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


class PositiveControlTests(unittest.TestCase):
    def test_full_driver_emits_evidence_and_returns_all_three_statuses(self):
        def archive(args, **kwargs):
            self.assertEqual(args[:2], ['git', 'archive'])
            with tarfile.open(fileobj=kwargs['stdout'], mode='w'):
                pass
        def build(source, dest, config, compiler, log):
            (dest / 'build').mkdir()
            return dest / 'python', {}
        def revision(ref):
            return {perf.ANCHOR: 'anchor', perf.HISTORICAL: 'history',
                    'HEAD': 'candidate', 'base': 'anchor'}[ref]
        for control_ok, candidate_slow, status, code in (
                (True, False, 'no_slowdown_observed', 0),
                (True, True, 'review_required', 2),
                (False, False, 'inconclusive_control', 1)):
            def command(args, **kwargs):
                if '--version' in args:
                    return 'test version'
                injected = '--positive-control' in args
                candidate = 'candidate' in Path(kwargs['env']['PYTHONPATH']).parts
                value = 60000 if injected and control_ok else 40000 if candidate and candidate_slow else 20000
                count = args[args.index('--samples') + 1]
                return json.dumps(dict(cold=value, samples={p: [value] * count for p in perf.PHASES},
                                       output_sha256='checked', request_repetitions=3 if injected else 1))
            with self.subTest(status=status), tempfile.TemporaryDirectory() as directory:
                output = Path(directory) / 'evidence'
                args = SimpleNamespace(output=output, base='base', head='HEAD', smoke=True, compiler='clang')
                with patch.object(perf, 'revision', side_effect=revision), \
                        patch.object(perf.subprocess, 'run', side_effect=archive), \
                        patch.object(perf, 'build', side_effect=build), \
                        patch.object(perf, 'command', side_effect=command), \
                        patch.object(perf.platform, 'platform', return_value='test'), \
                        patch.object(perf.platform, 'machine', return_value='test'), \
                        patch.object(perf.importlib.metadata, 'version', return_value='test'), \
                        redirect_stdout(StringIO()):
                    self.assertEqual(perf.run(args), code)
                report = json.loads((output / 'report.json').read_text())
                self.assertEqual(report['status'], status)
                self.assertEqual(report['positive_control']['detected'], control_ok)
                self.assertTrue(report['historical_control']['informative_only'])
                self.assertFalse(report['release_eligible'])
                self.assertEqual(report['variants']['positive_control']['commit'], 'anchor')
                self.assertEqual(report['variants']['positive_control']['request_repetitions'], 3)
                self.assertEqual(len(list((output / 'raw').glob('*.json'))), 240)
                self.assertIn('Historical comparison (informative)', (output / 'report.md').read_text())
                self.assertIn('positive_control', (output / 'samples.csv').read_text())

    def test_informative_history_cannot_approve_or_invalidate_positive_control(self):
        data = controlled_samples()
        rows, positive, historical, detected, status = perf.assess(data, perf.CONFIGS)
        self.assertTrue(detected)
        self.assertFalse(perf.historical_detected(historical, perf.CONFIGS))
        self.assertEqual(status, "no_slowdown_observed")
        self.assertEqual(rows, perf.compare(data))
        self.assertEqual(len([r for r in positive if r['metric'] == 'median']), 32)
        # A historical slowdown cannot rescue a removed injection.
        data['historical'], data['positive_control'] = data['positive_control'], data['historical']
        self.assertEqual(perf.assess(data, perf.CONFIGS)[-1], 'inconclusive_control')

    def test_every_case_configuration_and_round_is_required(self):
        for config in perf.CONFIGS:
            for case in perf.CASES:
                for round_name in ('ab0', 'ab1'):
                    data = controlled_samples()
                    data['positive_control'][round_name][f'{config}/{case}/total'] = [20000] * 100
                    with self.subTest(config=config, case=case, round=round_name):
                        self.assertEqual(perf.assess(data, perf.CONFIGS)[-1], 'inconclusive_control')
        rows = perf.assess(controlled_samples(), perf.CONFIGS)[1]
        self.assertFalse(perf.positive_detected([], perf.CONFIGS))
        self.assertFalse(perf.positive_detected(rows, []))
        self.assertFalse(perf.positive_detected([r for r in rows if r['metric'] == 'p95'], perf.CONFIGS))

    def test_candidate_slowdowns_remain_review_required_with_a_valid_control(self):
        data = controlled_samples()
        key = 'SMALL-Release/7-symbol-solve/total'
        data['head']['ab1'][key][-6:] = [40000] * 6
        rows, _, _, detected, status = perf.assess(data, perf.CONFIGS)
        self.assertTrue(detected)
        self.assertEqual(status, 'review_required')
        self.assertTrue(any(r['scenario'] == key and r['metric'] == 'p95' and
                            r['classification'] == ['indeterminate', 'slower'] for r in rows))

    def test_missing_control_samples_fail_closed(self):
        for mutation in ('role', 'pass', 'case'):
            data = controlled_samples()
            if mutation == 'role':
                del data['positive_control']
            elif mutation == 'pass':
                del data['positive_control']['ab1']
            else:
                del data['positive_control']['ab1']['LARGE-default/93-symbol-prepared/total']
            with self.subTest(mutation=mutation), self.assertRaises((KeyError, ValueError)):
                perf.assess(data, perf.CONFIGS)

    def test_control_uses_anchor_binary_without_aliasing_normal_samples(self):
        variants = perf.measurement_variants(dict(base='a', anchor='a', head='b', historical='c'))
        self.assertEqual(variants['base'], variants['anchor'])
        self.assertEqual(variants['positive_control'], ('a', 3))
        unique = list(dict.fromkeys(variants.values()))
        self.assertEqual(len(unique), 4)
        plan = perf.schedule(unique)
        for variant in unique:
            self.assertEqual([name for item, name in plan if item == variant],
                             ['aa0', 'aa1', 'aa2', 'aa3', 'ab0', 'ab1'])

    def test_injection_repeats_real_lifecycle_and_keeps_every_answer(self):
        events = []
        def transaction(phases=False):
            index = len(events) // 4
            events.extend(('input', 'solve', 'query', 'close'))
            return [index, False], [10, 12, 15, 20, 27] if phases else None
        self.assertIs(workload.repeat_transaction(transaction, 1), transaction)
        injected = workload.repeat_transaction(transaction, workload.POSITIVE_CONTROL_REQUESTS)
        answers, stamps = injected(True)
        self.assertEqual(events, ['input', 'solve', 'query', 'close'] * 3)
        self.assertEqual(answers, [0, False, 1, False, 2, False])
        self.assertEqual(stamps, [0, 6, 15, 30, 51])
        events.clear()
        answers, stamps = injected(False)
        self.assertEqual(answers, [0, False, 1, False, 2, False])
        self.assertIsNone(stamps)
        for invalid in (0, 2, 4):
            with self.assertRaises(ValueError):
                workload.repeat_transaction(transaction, invalid)


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
        unstarted = self.run_record(id=9, run_started_at=None, status="completed", conclusion="cancelled")
        self.assertEqual(gate.select_run([unstarted, self.run_record()], "a" * 40)["id"], 10)

    def test_missing_expired_or_ambiguous_artifact_is_refused(self):
        artifact = dict(name=gate.ARTIFACT_PREFIX + "a" * 40, expired=False)
        self.assertEqual(gate.require_artifact([artifact], "a" * 40), artifact)
        for artifacts in ([], [dict(artifact, expired=True)], [artifact, artifact]):
            with self.assertRaises(ValueError):
                gate.require_artifact(artifacts, "a" * 40)

    def test_locator_names_measured_commit_without_approving_performance(self):
        output = StringIO()
        responses = [dict(workflow_runs=[self.run_record()]),
                     dict(artifacts=[dict(name=gate.ARTIFACT_PREFIX + "a" * 40, expired=False)]),
                     dict(jobs=[dict(name="Python performance evidence", conclusion="success")])]
        with patch.object(gate, "revision", return_value="a" * 40) as revision, \
                patch.object(gate, "api", side_effect=responses), redirect_stdout(output):
            gate.check("measured-before-changelog")
        revision.assert_called_once_with("measured-before-changelog")
        self.assertIn("report available for " + "a" * 40, output.getvalue())
        self.assertIn("not performance approval", output.getvalue())
        self.assertIn("changelog PR", output.getvalue())

    def test_workflow_keeps_review_advisory_but_propagates_tooling_failures(self):
        workflow = (ROOT / ".github/workflows/python-performance.yml").read_text()
        step = workflow.split("      - name: Measure candidate,", 1)[1].split("      - name:", 1)[0]
        command = textwrap.dedent(step.split("        run: |\n", 1)[1])
        with tempfile.TemporaryDirectory() as directory:
            python = Path(directory) / "python"
            python.write_text('#!/bin/sh\ncase "$1" in\n'
                              'tools/check_python_performance.py) echo reference ;;\n'
                              'bench/python_perf.py) exit "$TEST_BENCH_STATUS" ;;\n'
                              '*) exit 99 ;;\nesac\n')
            python.chmod(0o755)
            for status in (0, 1, 2, 3):
                with self.subTest(status=status):
                    result = subprocess.run(["bash", "--noprofile", "--norc", "-eo", "pipefail", "-c", command],
                                            env=dict(os.environ, PATH=directory + os.pathsep + os.environ["PATH"],
                                                     TEST_BENCH_STATUS=str(status)), text=True, capture_output=True)
                    self.assertEqual(result.returncode, 0 if status in (0, 2) else status, result.stderr)
                    self.assertEqual("::warning::" in result.stdout, status == 2)


if __name__ == "__main__":
    unittest.main()
