"""Executable coverage of the opaque facade, not inferred from roadmap text."""

import hashlib
import json
import os
import re
from pathlib import Path
import tempfile
import threading
import unittest
from unittest.mock import patch
from types import SimpleNamespace
import subprocess
import sys
import textwrap

from maelys_datalog_next import (
    Capability, Engine, Predicate, PRED_EDB, PRED_IDB, PRED_QUERY, MaelysDatalogError,
)
from maelys_datalog_next import engine as binding


DOMAIN = "next_complete_facade"
DECLARATIONS = [
    Predicate("seed", 1, PRED_EDB),
    Predicate("blocked", 1, PRED_EDB),
    Predicate("extra", 1, PRED_EDB),
    Predicate("allow", 1, PRED_IDB | PRED_QUERY),
    Predicate("hidden", 1, PRED_IDB),
]
SOURCE = "allow(X) :- seed(X), not(blocked(X))."


def inputs(ruleset, *facts):
    edb = ruleset.edb()
    edb.add_facts(facts)
    return edb


def manifest(directory, sources):
    policies = []
    for i, source in enumerate(sources):
        name = f"policy-{i}.dl"
        data = source.encode("utf-8")
        (directory / name).write_bytes(data)
        policies.append(dict(
            policy_id=f"policy-{i}", domain=DOMAIN, file=name,
            sha256=hashlib.sha256(data).hexdigest(), mode="enforce", enabled=True,
            description="Python opaque facade test", queries=[dict(name="allow", arity=1)],
        ))
    path = directory / "manifest.json"
    path.write_text(json.dumps(dict(
        policy_set_id="next.facade", policy_set_version="1", manifest_version="1",
        default_profile="enforce", created_for="test", strict_loading=True,
        fail_closed=True, capabilities=[], policies=policies,
    )), encoding="utf-8")
    return path


class FacadeTest(unittest.TestCase):
    def setUp(self):
        self.engine = Engine()
        self.engine.register_domain(DOMAIN, DECLARATIONS)
        self.addCleanup(self.engine.close)

    def policy(self, source=SOURCE):
        return self.engine.load_inline_ruleset(DOMAIN, "next.facade", source)

    def test_explicit_input_storage_bounds_are_atomic_and_reusable(self):
        rules = self.policy()
        # Five entries overflow a capacity-four EDB before ANY native call;
        # never request a sixth item, even from an unbounded iterator.
        bounded = rules.edb(fact_capacity=4)
        consumed = []

        def too_many():
            for i in range(5):
                consumed.append(i)
                yield "seed", [i]
            self.fail("overflow detection consumed beyond the fifth item")

        constants_only = SimpleNamespace(MAELYS_DATALOG_STATUS_PAYLOAD_TOO_LARGE=-12,
                                         MAELYS_DATALOG_PUBLIC_MAX_TERMS=4)
        with patch.object(binding, "lib", constants_only):
            with self.assertRaisesRegex(MaelysDatalogError, "before deduplication"):
                bounded.add_facts(too_many())
        self.assertEqual(consumed, list(range(5)))
        self.assertEqual(len(bounded), 0)
        bounded.add_facts(("seed", [i]) for i in range(4))
        with self.assertRaises(MaelysDatalogError):
            bounded.add_facts([("seed", [5])])
        self.assertEqual(len(bounded), 4)
        # seed\0 + alice\0 = 11 bytes; distinct strings, not occurrences.
        edb = rules.edb(fact_capacity=2, text_capacity=11)
        with self.assertRaises(MaelysDatalogError) as error:
            edb.add_facts([("seed", ["alice"]), ("seed", ["bob"])])
        self.assertIn("text capacity exhausted", error.exception.message)
        self.assertEqual(len(edb), 0)
        for _ in range(3):
            edb.add_fact("seed", ["alice"])
            self.assertEqual(len(edb), 1)
            edb.clear()
        edb.add_fact("seed", ["alice"])
        with rules.solve(edb) as result:
            self.assertTrue(result.contains_fact("allow", ["alice"]))
        with self.assertRaises(TypeError):
            rules.edb(fact_capacity=True)
        with self.assertRaises(ValueError):
            rules.edb(fact_capacity=0)
        with self.assertRaises(ValueError):
            rules.edb(text_capacity=-1)
        with self.assertRaises(TypeError):
            rules.edb(text_capacity=1.5)
        with self.assertRaises(ValueError):
            rules.edb(text_capacity=self.engine.limits.input_edb_text_bytes + 1)

    def test_repeated_symbols_share_fixed_storage(self):
        rules = self.policy()
        self.assertEqual(self.engine.limits.input_edb_text_bytes, 40 * 1024)
        edb = rules.edb(fact_capacity=4, text_capacity=11)
        edb.add_facts([("seed", ["alice"]), ("seed", ["alice"])])
        edb.add_fact("seed", ["alice"])
        self.assertEqual(len(edb), 3)  # Stored entries still count duplicates.
        with rules.prepare() as session:
            with session.solve(edb) as result:
                self.assertTrue(result.contains_fact("allow", ["alice"]))
            with session.solve(edb) as result:
                self.assertTrue(result.contains_fact("allow", ["alice"]))

    def test_reset_reuses_input_storage_without_mutating_live_result(self):
        rules = self.policy()
        edb = rules.edb(fact_capacity=4, text_capacity=64)
        edb.add_fact("seed", ["alice"])
        with rules.prepare() as session:
            with session.solve(edb) as result:
                with self.assertRaises(RuntimeError):
                    edb.clear()
                pointer = edb._edb
                edb.reset()
                self.assertEqual(edb._edb, pointer)
                self.assertEqual(len(edb), 0)
                edb.add_fact("seed", ["bob"])
                self.assertTrue(result.contains_fact("allow", ["alice"]))
                with self.assertRaises(RuntimeError):
                    session.solve(edb)
            with session.solve(edb) as result:
                self.assertTrue(result.contains_fact("allow", ["bob"]))
            edb.reset()
            with session.solve(edb) as result:
                self.assertEqual(result.derived_fact_count(), 0)
        edb.close()
        with self.assertRaises(RuntimeError):
            edb.reset()

    def test_core_facade_coverage_does_not_silently_drift(self):
        # A new public core operation requires an explicit Python ownership/API
        # decision. Extension SDK authoring callbacks are a separate surface.
        coverage = {
            "status_name": "error formatting", "limit_get": "Engine.limits",
            "public_diagnostic_clear": "CFFI diagnostic utility",
            "domain_register": "Engine.register_domain",
            "policy_load_inline": "Engine.load_inline_ruleset",
            "policy_load_manifest": "Engine.load_manifest",
            "policy_count": "Ruleset.policy_count", "policy_fingerprint": "Ruleset.fingerprint",
            "policy_free": "Ruleset.close", "session_create": "Ruleset.prepare via configured",
            "session_config_create": "Ruleset.prepare",
            "session_config_set_required_capabilities": "Ruleset.prepare",
            "session_config_get_required_capabilities": "CFFI configuration inspection",
            "session_config_set_work_limit": "Ruleset.prepare",
            "session_config_get_work_limit": "CFFI configuration inspection",
            "session_config_set_explanation_workspace": "Ruleset.prepare explanations opt-in",
            "session_config_set_explanation_storage": "CFFI exclusive caller-owned workspace alternative",
            "session_config_free": "Ruleset.prepare finally",
            "session_create_configured": "Ruleset.prepare",
            "session_execution_fingerprint": "Session.execution_fingerprint",
            "session_fingerprint": "Session.fingerprint", "session_solve": "CFFI array solve alternative",
            "input_edb_create": "CFFI default-budget constructor", "input_edb_add_fact": "Edb.add_fact",
            "input_edb_create_with_capacity": "Ruleset.edb explicit bounded storage",
            "input_edb_storage_requirements": "CFFI caller-owned storage utility",
            "input_edb_init": "CFFI caller-owned storage utility",
            "input_edb_add_facts": "Edb.add_facts", "input_edb_count": "len(edb)",
            "input_edb_clear": "Edb.clear", "input_edb_free": "Edb.close",
            "session_solve_edb": "Session.solve",
            "session_free": "Session.close", "result_query": "SolveResult.contains_fact",
            "result_enumerate": "SolveResult.enumerate_raw",
            "result_derived_fact_count": "SolveResult.derived_fact_count",
            "result_symbol_text": "SolveResult.resolve_term",
            "result_explain_true_text": "SolveResult session-cached text when configured",
            "result_explain_false_text": "SolveResult session-cached text when configured",
            "result_explanation_storage_requirements": "SolveResult explanation workspace",
            "session_explanation_storage_bound": "Native only: reference session storage bound",
            "result_explain_text_in": "Native only: caller-owned one-shot alternative",
            "result_prepare_explanation": "SolveResult.explain_true/explain_false once",
            "prepared_explanation_text_size": "SolveResult cached text size",
            "prepared_explanation_write_text": "SolveResult text copy",
            "prepared_explanation_release": "SolveResult finally releases result lease",
            "result_free": "SolveResult.close",
        }
        engine_dir = Path(os.environ.get("MAELYS_DATALOG_ENGINE_DIR",
                                        Path(__file__).resolve().parents[3]))
        header = engine_dir / "include/maelys/datalog.h"
        exports = set(re.findall(r"MAELYS_DATALOG_API\s+[^;]+?\b(maelys_datalog_\w+)\s*\(",
                                 header.read_text(encoding="utf-8")))
        self.assertEqual(exports, {"maelys_datalog_" + name for name in coverage})
        for name in exports:
            self.assertTrue(callable(getattr(binding.lib, name)), name)

    def test_bridge_depends_only_on_consumer_header(self):
        builder = Path(__file__).resolve().parents[1] / "build_cffi.py"
        source = builder.read_text(encoding="utf-8")
        self.assertEqual(re.findall(r"#include\s+[<\"]([^>\"]+)[>\"]", source),
                         ["maelys/datalog.h"])
        self.assertNotIn("MAELYS_DATALOG_BACKEND_ABI_VERSION", source)
        self.assertNotIn("maelys_datalog_session_options_t", source)

    def test_reused_session_a_b_a_and_single_live_lease(self):
        rules = self.policy()
        a = inputs(rules, ("seed", ["alice"]))
        b = inputs(rules, ("seed", ["bob"]))
        with rules.prepare() as session:
            native = session._session
            fingerprint = session.fingerprint
            first = session.solve(a)
            before = first.explain_true("allow", ["alice"])
            with self.assertRaisesRegex(RuntimeError, "Close the current result"):
                session.solve(b)
            first.close()
            with session.solve(b) as second:
                self.assertFalse(second.contains_fact("allow", ["alice"]))
                self.assertTrue(second.contains_fact("allow", ["bob"]))
            with session.solve(a) as third:
                self.assertEqual(third.explain_true("allow", ["alice"]), before)
            self.assertEqual(session._session, native)
            self.assertEqual(session.fingerprint, fingerprint)

    def test_failed_batch_preserves_session_and_diagnostic(self):
        rules = self.policy()
        with rules.prepare() as session:
            with self.assertRaises(MaelysDatalogError) as failure:
                session.solve(inputs(rules, ("seed", ["wrong", "arity"])))
            error = failure.exception
            self.assertEqual(error.code, error.status)
            self.assertEqual(error.diagnostic.source, 2)
            self.assertEqual(error.diagnostic.phase, "input")
            self.assertEqual(error.diagnostic.message, error.message)
            self.assertEqual(error.diagnostic.hint, error.hint)
            with session.solve(inputs(rules, ("seed", ["ok"]))) as result:
                self.assertTrue(result.contains_fact("allow", ["ok"]))

    def test_capabilities_and_execution_identity_are_explicit(self):
        rules = self.policy()
        with rules.prepare() as a, rules.prepare(required_capabilities=Capability.EXPLAIN_FALSE) as b:
            self.assertEqual(a.fingerprint, b.fingerprint)
            self.assertNotEqual(a.execution_fingerprint, b.execution_fingerprint)
            self.assertRegex(a.execution_fingerprint, r"^[0-9a-f]{64}$")
            with a.solve(inputs(rules, ("seed", ["alice"]))) as result:
                self.assertEqual(result.execution_fingerprint, a.execution_fingerprint)
                self.assertEqual(result.fingerprint, a.fingerprint)
        with rules.prepare(required_capabilities=Capability.EXPLAIN_FALSE) as session:
            with session.solve(inputs(rules, ("seed", ["bob"]), ("blocked", ["bob"]))) as result:
                self.assertIn("status=complete", result.explain_false("allow", ["bob"]))
        with self.assertRaises(MaelysDatalogError):
            rules.prepare(required_capabilities=1 << 63)
        for value in (-1, 1 << 64, True, "5"):
            with self.assertRaises((TypeError, ValueError)):
                rules.prepare(work_limit=value)
        # Current reference backend does NOT advertise WORK_LIMIT. The wrapper
        # must surface UNSUPPORTED, not claim to enforce a supplied budget.
        for kwargs in ({"work_limit": 1}, {"required_capabilities": Capability.WORK_LIMIT}):
            with self.assertRaises(MaelysDatalogError) as failure:
                rules.prepare(**kwargs)
            self.assertEqual(failure.exception.status, -5)

    def test_parse_diagnostic_has_source_coordinates(self):
        with self.assertRaises(MaelysDatalogError) as failure:
            self.policy("\nallow(X) :- nonexistent(X).")
        detail = failure.exception.diagnostic
        self.assertEqual(detail.source, 1)
        self.assertNotEqual(detail.code, 0)
        self.assertEqual(detail.line, 2)
        self.assertGreater(detail.column, 0)

    def test_explanations_preserve_answers_and_false_status(self):
        rules = self.policy()
        with rules.solve(inputs(rules, ("seed", ["alice"]), ("seed", ["bob"]),
                                ("blocked", ["bob"]))) as result:
            before = result.enumerate_predicate_facts("allow", 1)
            self.assertTrue(result.explain_true("allow", ["alice"]).startswith(
                "MAELYS-DATALOG-v2\ndocument=why-true\nstatus=complete\n"))
            text = result.explain_false("allow", ["bob"])
            self.assertTrue(text.startswith(
                "MAELYS-DATALOG-v2\ndocument=why-false\nstatus=complete\n"))
            self.assertIn("negative-contradicted", text)
            self.assertTrue(result.explain_false("allow", ["alice"]).startswith(
                "MAELYS-DATALOG-v2\ndocument=why-false\nstatus=not-applicable\n"))
            self.assertEqual(result.explain_false("allow", ["bob"]), text)
            for method in (result.explain_true, result.explain_false):
                with self.assertRaises(MaelysDatalogError) as failure:
                    method("allow", ["not-in-vocabulary"])
                self.assertEqual(failure.exception.status, -3)
            self.assertEqual(result.enumerate_predicate_facts("allow", 1), before)

    def test_why_false_preserves_truncation(self):
        rules = self.policy("allow(X) :- seed(X), extra(X).\n" * 17)
        with rules.solve(inputs(rules, ("seed", ["alice"]))) as result:
            text = result.explain_false("allow", ["alice"])
            self.assertTrue(text.startswith(
                "MAELYS-DATALOG-v2\ndocument=why-false\nstatus=truncated\n"))
            self.assertIn("limit-hits=diagnostics", text)

    def test_filters_are_ground_only_and_patterns_are_not_atoms(self):
        rules = self.policy('allow(X) :- seed(X), starts_with(X, "docs/"), '
                            'ends_with(X, ".md"), contains(X, "api").')
        edb = inputs(rules, *(('seed', [s]) for s in
                     ("docs/api.md", "src/api.md", "docs/api.txt")))
        with rules.solve(edb) as result:
            self.assertEqual(result.enumerate_predicate_facts("allow", 1), [("docs/api.md",)])
            self.assertIn("filter-false", result.explain_false("allow", ["src/api.md"]))
        with self.assertRaises(MaelysDatalogError):
            self.policy('allow(X) :- starts_with(X, "docs/").')
        # The 80-byte pattern is not subject to the policy atom's 63-byte limit.
        self.policy('allow(X) :- seed(X), contains(X, "' + "x" * 80 + '").')
        with rules.prepare() as session:
            with self.assertRaises(MaelysDatalogError):
                session.solve(inputs(rules, ("seed", [7])))
            # A type error must not become a false filter result, nor poison a retry.
            with session.solve(inputs(rules, ("seed", ["docs/api.md"]))) as result:
                self.assertTrue(result.contains_fact("allow", ["docs/api.md"]))

    def test_raw_terms_require_same_live_result(self):
        rules = self.policy()
        first = rules.solve(inputs(rules, ("seed", ["éclair"]), ("seed", [True]), ("seed", [7])))
        rows = first.enumerate_raw("allow", 1)
        self.assertEqual({row[0].kind for row in rows}, {"symbol", "boolean", "integer"})
        copied = first.enumerate_predicate_facts("allow", 1)
        self.assertEqual({row[0].resolve() for row in rows}, {"éclair", True, 7})
        second = rules.solve(inputs(rules, ("seed", ["another"])))
        with self.assertRaisesRegex(ValueError, "another result"):
            second.resolve_term(rows[0][0])
        first.close()
        for row in rows:
            with self.assertRaises(RuntimeError):
                row[0].resolve()
        self.assertIn(("éclair",), copied)

    def test_parent_close_closes_prepared_sessions_and_results(self):
        rules = self.policy()
        session = rules.prepare()
        result = session.solve(inputs(rules, ("seed", ["alice"])))
        self.engine.close()
        for operation in (lambda: session.fingerprint, lambda: rules.fingerprint,
                          lambda: result.explain_false("allow", ["alice"])):
            with self.assertRaises(RuntimeError):
                operation()
        result.close()
        session.close()
        rules.close()

        # Collect a genuinely abandoned owner graph in a subprocess. Native
        # storage is intentionally not freed by __del__; process exit reclaims
        # it without leaking into the rest of this test interpreter.
        program = textwrap.dedent('''
            import gc, json, sys, warnings, weakref
            from maelys_datalog_next import Engine, Predicate, PRED_EDB, PRED_IDB, PRED_QUERY
            from maelys_datalog_next import engine as binding

            def abandoned(closed):
                engine = Engine()
                engine.register_domain("gc_warning_test", [
                    Predicate("seed", 1, PRED_EDB),
                    Predicate("allow", 1, PRED_IDB | PRED_QUERY)])
                rules = engine.load_inline_ruleset("gc_warning_test", "gc", "allow(X) :- seed(X).")
                edb = rules.edb()
                session = rules.prepare()
                result = session.solve(edb)
                refs = [weakref.ref(obj) for obj in (edb, session, result)]
                if closed:
                    result.close()
                    engine.close()
                return refs

            calls, unraisable = [], []
            class NoNative:
                def __getattr__(self, name):
                    calls.append(name)
                    raise AssertionError("native access during finalization: " + name)
            gc.disable()
            refs = abandoned(sys.argv[1] == "closed")
            binding.lib = NoNative()
            sys.unraisablehook = lambda event: unraisable.append(str(event.exc_value))
            with warnings.catch_warnings(record=True) as seen:
                warnings.simplefilter("always", ResourceWarning)
                # __init__ can fail before a native handle exists.
                for cls in (binding.Session, binding.Edb, binding.SolveResult):
                    partial = object.__new__(cls)
                    del partial
                gc.collect()
            assert not calls, calls
            assert not unraisable, unraisable
            assert all(ref() is None for ref in refs)
            assert all(item.category is ResourceWarning for item in seen)
            print(json.dumps([str(item.message) for item in seen]))
        ''')
        for state in ("open", "closed"):
            with self.subTest(finalization=state):
                child = subprocess.run([sys.executable, "-c", program, state],
                                       capture_output=True, text=True, check=True)
                messages = json.loads(child.stdout)
                self.assertEqual(len(messages), 3 if state == "open" else 0)
                if state == "open":
                    self.assertEqual({message.split(";")[0] for message in messages},
                                     {"Unclosed Edb", "Unclosed Session", "Unclosed SolveResult"})

    def test_thread_confinement_rejects_use_and_close(self):
        rules = self.policy()
        session = rules.prepare()
        result = session.solve(inputs(rules, ("seed", ["alice"])))
        failures = []

        def other_thread():
            for operation in (lambda: result.contains_fact("allow", ["alice"]),
                              result.close, session.close, rules.close, self.engine.close):
                try:
                    operation()
                except RuntimeError as error:
                    failures.append(str(error))

        thread = threading.Thread(target=other_thread)
        thread.start()
        thread.join()
        self.assertEqual(len(failures), 5)
        self.assertTrue(all("creating thread" in message for message in failures))
        self.assertTrue(result.contains_fact("allow", ["alice"]))

    def test_manifest_policy_local_atoms_and_multi_policy_selection(self):
        with tempfile.TemporaryDirectory() as directory:
            path = manifest(Path(directory), ['allow("alice") :- seed("alice").',
                                               'allow("bob") :- seed("bob").'])
            with self.assertRaises(MaelysDatalogError):
                self.engine.load_manifest(path)
            with self.engine.load_manifest(path, allow_undeclared_policy_atoms=True) as rules:
                self.assertEqual(rules.policy_count, 2)
                self.assertRegex(rules.fingerprint, r"^[0-9a-f]{64}$")
                edb = inputs(rules, ("seed", ["alice"]), ("seed", ["bob"]))
                with rules.solve(edb, policy_index=0) as first, rules.solve(edb, policy_index=1) as second:
                    self.assertEqual(first.enumerate_predicate_facts("allow", 1), [("alice",)])
                    self.assertEqual(second.enumerate_predicate_facts("allow", 1), [("bob",)])
                for index in (-1, 2):
                    with self.assertRaises(IndexError):
                        rules.prepare(index)
                with self.assertRaises(TypeError):
                    rules.prepare(True)
            # Opt-in must not leak into the domain, inline loads, or later manifests.
            with self.assertRaises(MaelysDatalogError):
                self.engine.load_manifest(path)
            with self.assertRaises(MaelysDatalogError):
                self.policy('allow("alice") :- seed("alice").')

    def test_manifest_permissions_are_independent(self):
        self.assertEqual(binding.lib.MAELYS_DATALOG_PUBLIC_ALLOW_NONE, 0)
        with tempfile.TemporaryDirectory() as directory:
            for local in (False, True):
                source = 'allow(X) :- seed("alice"), seed(X).' if local else SOURCE
                path = manifest(Path(directory), [source])
                document = json.loads(path.read_text())
                for test_only in (False, True):
                    document["policies"][0]["mode"] = "test_only" if test_only else "enforce"
                    path.write_text(json.dumps(document))
                    for allow_test in (False, True):
                        for allow_atoms in (False, True):
                            options = dict(allow_test_only=allow_test,
                                           allow_undeclared_policy_atoms=allow_atoms)
                            with self.subTest(local=local, mode=test_only, **options):
                                denied_mode = test_only and not allow_test
                                if denied_mode or (local and not allow_atoms):
                                    with self.assertRaises(MaelysDatalogError) as failure:
                                        self.engine.load_manifest(path, **options)
                                    self.assertEqual(failure.exception.status, -10 if denied_mode else -2)
                                else:
                                    with self.engine.load_manifest(path, **options) as rules:
                                        with rules.solve(inputs(rules, ("seed", ["alice"]))) as result:
                                            self.assertTrue(result.contains_fact("allow", ["alice"]))
                    # Omitting both keywords keeps the same default rejection.
                    if local or test_only:
                        with self.assertRaises(MaelysDatalogError):
                            self.engine.load_manifest(path)
            with self.assertRaises(MaelysDatalogError):
                self.policy('allow(X) :- seed("alice"), seed(X).')
            for options in ({"allow_test_only": 1}, {"allow_undeclared_policy_atoms": 1},
                            {"allow_none": True}):
                with self.assertRaises(TypeError):
                    self.engine.load_manifest(path, **options)

    def test_manifest_hash_and_query_whitelist_enforced(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            path = manifest(root, ["allow(X) :- seed(X). hidden(X) :- seed(X)."])
            with self.engine.load_manifest(path) as rules:
                fingerprint = rules.fingerprint
                with rules.solve(inputs(rules, ("seed", ["alice"]))) as result:
                    with self.assertRaises(MaelysDatalogError):
                        result.contains_fact("hidden", ["alice"])
                with self.engine.load_manifest(path) as again:
                    self.assertEqual(again.fingerprint, fingerprint)
            (root / "policy-0.dl").write_text(SOURCE, encoding="utf-8")
            with self.assertRaises(MaelysDatalogError) as failure:
                self.engine.load_manifest(path)
            self.assertEqual(failure.exception.diagnostic.source, 1)
            self.assertNotEqual(failure.exception.diagnostic.code, 0)


if __name__ == "__main__":
    unittest.main()
