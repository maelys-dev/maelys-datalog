"""Parity and lifecycle gates for the experimental opaque-facade binding."""

from __future__ import annotations

from pathlib import Path
import sys
import unittest
import os
import json
import subprocess
from importlib.machinery import EXTENSION_SUFFIXES
from dataclasses import FrozenInstanceError


ROOT = Path(__file__).resolve().parents[3]
sys.path.insert(0, str(ROOT / "bindings" / "python"))
sys.path.insert(0, str(ROOT / "bindings" / "python-next"))

CURRENT_EXTENSION_AVAILABLE = any(
    (ROOT / "bindings/python/maelys_datalog" / ("_maelys_cffi" + suffix)).is_file()
    for suffix in EXTENSION_SUFFIXES
)
CURRENT_PARITY_CHILD = sys.argv[1:] == ["--current-parity"]
if CURRENT_PARITY_CHILD:
    import maelys_datalog as current  # noqa: E402
else:
    import maelys_datalog_next as next_binding  # noqa: E402


POLICY = (
    "can_read(User, Doc) :-\n"
    "    owns(User, Doc) or delegated(User, Doc),\n"
    "    not(blocked(User)).\n"
    "has_any_document(User) :- owns(User, _).\n"
    "allow(User, Doc) :- user(User), can_read(User, Doc).\n"
)
FACTS = [
    ("user", ["alice"]),
    ("user", ["bob"]),
    ("user", ["mallory"]),
    ("owns", ["alice", "roadmap.pdf"]),
    ("delegated", ["bob", "roadmap.pdf"]),
    ("owns", ["mallory", "roadmap.pdf"]),
    ("blocked", ["mallory"]),
]
QUERIES = [
    ("allow", ["alice", "roadmap.pdf"]),
    ("allow", ["bob", "roadmap.pdf"]),
    ("allow", ["mallory", "roadmap.pdf"]),
    ("has_any_document", ["alice"]),
    ("has_any_document", ["bob"]),
]
EXPECTED = [True, True, False, True, False]


def predicates(binding):
    return [
        binding.Predicate("user", 1, binding.PRED_EDB),
        binding.Predicate("owns", 2, binding.PRED_EDB),
        binding.Predicate("delegated", 2, binding.PRED_EDB),
        binding.Predicate("blocked", 1, binding.PRED_EDB),
        binding.Predicate("can_read", 2, binding.PRED_IDB),
        binding.Predicate(
            "has_any_document", 1, binding.PRED_IDB | binding.PRED_QUERY
        ),
        binding.Predicate("allow", 2, binding.PRED_IDB | binding.PRED_QUERY),
    ]


def evaluate(binding, domain):
    with binding.Engine() as engine:
        profile = os.environ.get("MAELYS_DATALOG_EXPECT_PROFILE")
        if profile:
            assert profile in ("small", "large"), profile
            assert engine.limits.max_edb_facts == (2048 if profile == "large" else 1024)
        engine.register_domain(domain, predicates(binding))
        ruleset = engine.load_inline_ruleset(domain, "documents.main", POLICY)
        edb = ruleset.edb()
        for name, values in FACTS:
            edb.add_fact(name, values)
        result = ruleset.solve(edb)
        answers = [
            result.contains_fact(predicate, terms)
            for predicate, terms in QUERIES
        ]
        rows = result.enumerate_predicate_facts("allow", 2)
        return answers, rows


class PythonNextTest(unittest.TestCase):
    @unittest.skipUnless(CURRENT_EXTENSION_AVAILABLE or os.environ.get("MAELYS_DATALOG_REQUIRE_PARITY") == "1",
                         "build the current Python binding to run the parity gate")
    def test_document_access_matches_current_binding(self):
        self.assertTrue(CURRENT_EXTENSION_AVAILABLE,
                        "Required legacy parity extension is missing; build bindings/python first")
        next_answers, next_rows = evaluate(next_binding, "next_access_parity")
        completed = subprocess.run(
            [sys.executable, str(Path(__file__).resolve()), "--current-parity"],
            text=True, capture_output=True, check=True,
        )
        current_answers, current_rows = json.loads(completed.stdout)
        self.assertEqual(next_answers, EXPECTED)
        self.assertEqual(next_answers, current_answers)
        self.assertEqual(set(next_rows), {tuple(row) for row in current_rows})

    def test_two_open_results_can_be_queried(self):
        binding = next_binding
        with binding.Engine() as engine:
            domain = "next_access_two_results"
            engine.register_domain(domain, predicates(binding))
            ruleset = engine.load_inline_ruleset(domain, "documents.main", POLICY)
            first_edb = ruleset.edb()
            first_edb.add_fact("user", ["alice"])
            first_edb.add_fact("owns", ["alice", "roadmap.pdf"])
            first = ruleset.solve(first_edb)

            second_edb = ruleset.edb()
            second_edb.add_fact("user", ["bob"])
            second_edb.add_fact("delegated", ["bob", "roadmap.pdf"])
            second = ruleset.solve(second_edb)
            self.assertTrue(first.contains_fact("allow", ["alice", "roadmap.pdf"]))
            self.assertTrue(second.contains_fact("allow", ["bob", "roadmap.pdf"]))
            self.assertFalse(first.contains_fact("allow", ["bob", "roadmap.pdf"]))
            first.close()
            self.assertTrue(second.contains_fact("allow", ["bob", "roadmap.pdf"]))

    def test_native_input_error_is_deferred_to_solve(self):
        binding = next_binding
        with binding.Engine() as engine:
            domain = "next_access_bad_fact"
            engine.register_domain(domain, predicates(binding))
            ruleset = engine.load_inline_ruleset(domain, "documents.main", POLICY)
            edb = ruleset.edb()
            edb.add_fact("undeclared", ["alice"])
            with self.assertRaisesRegex(binding.MaelysDatalogError, "index 0: unknown predicate undeclared/1"):
                ruleset.solve(edb)

    def test_batch_matches_single_adds_and_counts_nonqueryable_derivations(self):
        binding = next_binding
        with binding.Engine() as engine:
            domain = "next_access_batch"
            engine.register_domain(domain, predicates(binding))
            ruleset = engine.load_inline_ruleset(domain, "documents.main", POLICY)
            batch = ruleset.edb()
            batch.add_fact(*FACTS[0])
            batch.add_facts((name, values) for name, values in FACTS[1:])
            # A duplicate input must not duplicate derived facts.
            batch.add_facts([FACTS[0]])
            result = ruleset.solve(batch)
            self.assertEqual([result.contains_fact(*q) for q in QUERIES], EXPECTED)
            self.assertEqual(result.derived_fact_count(), 6)  # 2 can_read + 2 has_any_document + 2 allow
            self.assertEqual(len(result.enumerate_predicate_facts("allow", 2)), 2)
            with self.assertRaises(binding.MaelysDatalogError):
                result.enumerate_predicate_facts("can_read", 2)
            with self.assertRaises(RuntimeError):
                batch.add_facts([])
            result.close()
            with self.assertRaises(RuntimeError):
                result.derived_fact_count()
            self.assertEqual(ruleset.solve(ruleset.edb()).derived_fact_count(), 0)

    def test_add_facts_is_atomic_for_bad_values_and_failed_iteration(self):
        binding = next_binding
        with binding.Engine() as engine:
            domain = "next_batch_atomic"
            engine.register_domain(domain, predicates(binding))
            ruleset = engine.load_inline_ruleset(domain, "documents.main", POLICY)
            edb = ruleset.edb()
            edb.add_fact("user", ["alice"])
            invalid_items = [
                ("owns", [object(), "document"]),
                ("owns", ["alice", "document\0suffix"]),
                ("owns", ["alice", 1 << 64]),
                ("owns\0suffix", ["alice", "document"]),
                "owns",
            ]
            for invalid in invalid_items:
                with self.assertRaises((TypeError, ValueError, OverflowError)):
                    edb.add_facts([("owns", ["alice", "stale"]), invalid])

            def interrupted():
                yield "owns", ["alice", "stale"]
                raise RuntimeError("iteration failed")

            with self.assertRaisesRegex(RuntimeError, "iteration failed"):
                edb.add_facts(interrupted())
            terms = ["alice", "fresh"]
            edb.add_facts([("owns", terms)])
            terms[1] = "mutated"
            result = ruleset.solve(edb)
            self.assertTrue(result.contains_fact("allow", ["alice", "fresh"]))
            self.assertFalse(result.contains_fact("allow", ["alice", "stale"]))
            self.assertFalse(result.contains_fact("allow", ["alice", "mutated"]))

    def test_batch_arity_error_reports_index_and_expected_arity(self):
        binding = next_binding
        with binding.Engine() as engine:
            domain = "next_batch_arity"
            engine.register_domain(domain, predicates(binding))
            ruleset = engine.load_inline_ruleset(domain, "documents.main", POLICY)
            edb = ruleset.edb()
            edb.add_facts([("user", ["alice"]), ("owns", ["alice", "doc", "extra"])])
            with self.assertRaises(binding.MaelysDatalogError) as caught:
                ruleset.solve(edb)
            self.assertEqual(caught.exception.code, -2)
            self.assertEqual(caught.exception.message,
                             "Invalid fact at index 1: owns expects 2 arguments, received 3.")
            self.assertIn("zero-based", caught.exception.hint)

    def test_limits_are_native_and_capacity_errors_are_causal(self):
        binding = next_binding
        with binding.Engine() as engine:
            limits = engine.limits
            expected_profile = os.environ.get("MAELYS_DATALOG_EXPECT_PROFILE")
            if expected_profile:
                self.assertIn(expected_profile, ("small", "large"))
                self.assertEqual(limits.max_edb_facts, 2048 if expected_profile == "large" else 1024)
                self.assertEqual(limits.max_facts_per_pred, 256 if expected_profile == "large" else 64)
            self.assertEqual(limits.max_arity, 4)
            with self.assertRaises(FrozenInstanceError):
                limits.max_arity = 10
            domain = "next_batch_capacity"
            engine.register_domain(domain, predicates(binding))
            ruleset = engine.load_inline_ruleset(domain, "documents.main", POLICY)
            edb = ruleset.edb()
            edb.add_facts(("user", [i]) for i in range(limits.max_facts_per_pred + 1))
            with self.assertRaisesRegex(binding.MaelysDatalogError,
                                        f"index {limits.max_facts_per_pred}.*per-predicate fact limit"):
                ruleset.solve(edb)
            too_many = ruleset.edb()
            with self.assertRaisesRegex(binding.MaelysDatalogError, "before deduplication"):
                too_many.add_facts(("user", [0]) for _ in range(limits.max_edb_facts + 1))
            self.assertEqual(len(too_many), 0)
            self.assertEqual(ruleset.solve(ruleset.edb()).derived_fact_count(), 0)

    def test_native_edb_copy_atomicity_clear_and_lifetime(self):
        binding = next_binding
        with binding.Engine() as engine:
            domain = "next_native_edb"
            engine.register_domain(domain, predicates(binding))
            ruleset = engine.load_inline_ruleset(domain, "documents.main", POLICY)
            edb = ruleset.edb()
            self.assertFalse(hasattr(edb, "_facts"))
            values = ["alice", "roadmap.pdf"]
            edb.add_fact("owns", values)
            values[0] = "mallory"
            edb.add_fact("user", ["alice"])
            self.assertEqual(len(edb), 2)
            oversized = "é" * (engine.limits.max_string_bytes // 2 + 1)
            with self.assertRaises(binding.MaelysDatalogError) as caught:
                edb.add_facts([("user", ["bob"]), ("user", [oversized])])
            self.assertEqual(caught.exception.code, -12)
            self.assertEqual(len(edb), 2)
            result = ruleset.solve(edb)
            edb.close()
            edb.close()
            self.assertTrue(result.contains_fact("allow", ["alice", "roadmap.pdf"]))
            with self.assertRaises(RuntimeError):
                len(edb)
            result.close()
            retry = ruleset.edb()
            retry.add_fact("not_in_domain", ["alice"])
            with self.assertRaises(binding.MaelysDatalogError):
                ruleset.solve(retry)
            retry.clear()
            self.assertEqual(len(retry), 0)
            retry.add_facts(FACTS)
            with ruleset.solve(retry) as result:
                self.assertTrue(result.contains_fact("allow", ["alice", "roadmap.pdf"]))
            with self.assertRaises(RuntimeError):
                retry.clear()  # Python keeps its successful-solve freeze.
            ruleset.close()
            self.assertTrue(retry._closed)
            retry.close()  # Parent closure already freed the native handle.

    def test_native_edb_iterator_closure_never_uses_freed_handle(self):
        binding = next_binding
        with binding.Engine() as engine:
            domain = "next_edb_iterator_closure"
            engine.register_domain(domain, predicates(binding))
            ruleset = engine.load_inline_ruleset(domain, "documents.main", POLICY)
            edb = ruleset.edb()

            def closing_iterator():
                yield ("user", ["alice"])
                edb.close()

            with self.assertRaises(RuntimeError):
                edb.add_facts(closing_iterator())

    def test_utf8_runtime_symbol_and_ownership(self):
        binding = next_binding
        with binding.Engine() as engine:
            domain = "next_access_unicode"
            engine.register_domain(
                domain, [
                    binding.Predicate("observed", 1, binding.PRED_EDB),
                    binding.Predicate(
                        "allow", 1, binding.PRED_IDB | binding.PRED_QUERY
                    ),
                ],
            )
            ruleset = engine.load_inline_ruleset(
                domain, "unicode.main", "allow(X) :- observed(X).\n"
            )
            edb = ruleset.edb()
            edb.add_fact("observed", ['café\n"'])
            result = ruleset.solve(edb)
            self.assertTrue(result.contains_fact("allow", ['café\n"']))
            self.assertEqual(
                result.enumerate_predicate_facts("allow", 1), [('café\n"',)]
            )
        with self.assertRaises(RuntimeError):
            result.contains_fact("allow", ['café\n"'])

    def test_declared_policy_atom_is_available_to_inline_loader(self):
        binding = next_binding
        with binding.Engine() as engine:
            domain = "next_access_policy_atom"
            engine.register_domain(
                domain,
                [
                    binding.Predicate("classified", 2, binding.PRED_EDB),
                    binding.Predicate(
                        "allow", 1, binding.PRED_IDB | binding.PRED_QUERY
                    ),
                ],
                atoms=["public"],
            )
            ruleset = engine.load_inline_ruleset(
                domain, "atoms.main",
                'allow(X) :- classified(X, "public").\n',
            )
            edb = ruleset.edb()
            edb.add_fact("classified", ["roadmap.pdf", "public"])
            result = ruleset.solve(edb)
            self.assertTrue(result.contains_fact("allow", ["roadmap.pdf"]))


if __name__ == "__main__":
    if CURRENT_PARITY_CHILD:
        print(json.dumps(evaluate(current, "next_access_parity")))
    else:
        unittest.main()
