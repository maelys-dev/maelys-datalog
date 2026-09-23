"""Convenience declarations preserve the general Predicate contract."""
from dataclasses import FrozenInstanceError
import unittest

from maelys_datalog import (
    Engine, Predicate, PRED_EDB, PRED_IDB, PRED_QUERY, PRED_POLICY_FACT,
    MaelysDatalogError,
)


class PredicateBuildersTest(unittest.TestCase):
    def test_exact_flags_and_immutable_values(self):
        for build, flags in ((Predicate.edb, PRED_EDB),
                             (Predicate.edb_query, PRED_EDB | PRED_QUERY),
                             (Predicate.idb, PRED_IDB),
                             (Predicate.idb_query, PRED_IDB | PRED_QUERY),
                             (Predicate.policy_fact, PRED_POLICY_FACT),
                             (Predicate.policy_fact_query, PRED_POLICY_FACT | PRED_QUERY)):
            with self.subTest(flags=flags):
                declaration = build(name="example", arity=2)
                self.assertEqual(declaration, Predicate("example", 2, flags))
                self.assertEqual(hash(declaration), hash(Predicate("example", 2, flags)))
                with self.assertRaises(FrozenInstanceError):
                    declaration.flags = 0
        self.assertFalse(hasattr(Predicate, "query"))

    def test_class_methods_preserve_subclasses(self):
        class CustomPredicate(Predicate):
            pass
        for build in (CustomPredicate.edb, CustomPredicate.edb_query,
                      CustomPredicate.idb, CustomPredicate.idb_query,
                      CustomPredicate.policy_fact, CustomPredicate.policy_fact_query):
            self.assertIsInstance(build("example", 1), CustomPredicate)

    def test_registration_still_owns_validation(self):
        with Engine() as engine:
            for build in (Predicate.edb, Predicate.edb_query, Predicate.idb,
                          Predicate.idb_query, Predicate.policy_fact, Predicate.policy_fact_query):
                with self.subTest(build=build.__name__):
                    bad = build("bad", engine.limits.max_arity + 1)
                    with self.assertRaises(MaelysDatalogError):
                        engine.register_domain("predicate_builders_bad", [bad])
                    bad = build("", 1)
                    with self.assertRaises(TypeError):
                        engine.register_domain("predicate_builders_bad", [bad])
                    bad = build("bad", True)
                    with self.assertRaises(TypeError):
                        engine.register_domain("predicate_builders_bad", [bad])

    def test_query_requires_an_origin(self):
        with Engine() as engine:
            engine.register_domain("predicate_query_only", [Predicate("allow", 1, PRED_QUERY)])
            with self.assertRaises(MaelysDatalogError):
                engine.load_inline_ruleset("predicate_query_only", "invalid", "allow(X) :- allow(X).")

    def test_query_does_not_change_fact_origin(self):
        with Engine() as engine:
            engine.register_domain("predicate_origins_python", [
                Predicate.edb_query("observed", 1),
                Predicate.policy_fact("fixed", 1),
                Predicate.policy_fact_query("trusted", 1),
                Predicate.idb_query("allow", 1),
            ], atoms=["alice"])
            rules = engine.load_inline_ruleset("predicate_origins_python", "origins",
                'fixed("alice"). trusted("alice"). allow(X) :- observed(X), fixed(X).')
            edb = rules.edb()
            edb.add_fact("observed", ["alice"])
            with rules.solve(edb) as result:
                for predicate in ("observed", "trusted", "allow"):
                    self.assertTrue(result.contains_fact(predicate, ["alice"]))
                with self.assertRaises(MaelysDatalogError):
                    result.contains_fact("fixed", ["alice"])
            bad = rules.edb()
            bad.add_fact("trusted", ["alice"])
            with self.assertRaises(MaelysDatalogError):
                rules.solve(bad)

    def test_register_solve_and_query(self):
        with Engine() as engine:
            engine.register_domain("predicate_builders_python", [
                Predicate.edb("seed", 1),
                Predicate.idb("hidden", 1),
                Predicate.idb_query("allow", 1),
            ])
            rules = engine.load_inline_ruleset("predicate_builders_python", "builders",
                "hidden(X) :- seed(X). allow(X) :- hidden(X).")
            edb = rules.edb()
            edb.add_fact("seed", ["alice"])
            with rules.solve(edb) as result:
                self.assertTrue(result.contains_fact("allow", ["alice"]))
                self.assertEqual(result.derived_fact_count(), 2)
