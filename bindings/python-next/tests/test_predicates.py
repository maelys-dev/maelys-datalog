"""Convenience declarations preserve the general Predicate contract."""
from dataclasses import FrozenInstanceError
import unittest

from maelys_datalog_next import (
    Engine, Predicate, PRED_EDB, PRED_IDB, PRED_QUERY, PRED_POLICY_FACT,
    MaelysDatalogError,
)


class PredicateBuildersTest(unittest.TestCase):
    def test_exact_flags_and_immutable_values(self):
        for build, flags in ((Predicate.edb, PRED_EDB),
                             (Predicate.idb, PRED_IDB),
                             (Predicate.idb_query, PRED_IDB | PRED_QUERY)):
            with self.subTest(flags=flags):
                declaration = build(name="example", arity=2)
                self.assertEqual(declaration, Predicate("example", 2, flags))
                self.assertEqual(hash(declaration), hash(Predicate("example", 2, flags)))
                with self.assertRaises(FrozenInstanceError):
                    declaration.flags = 0
        # Other combinations stay explicit; QUERY is not a third fact category.
        self.assertEqual(Predicate("fixed", 1, PRED_POLICY_FACT).flags, PRED_POLICY_FACT)
        self.assertEqual(Predicate("input_query", 1, PRED_EDB | PRED_QUERY).flags,
                         PRED_EDB | PRED_QUERY)

    def test_class_methods_preserve_subclasses(self):
        class CustomPredicate(Predicate):
            pass
        for build in (CustomPredicate.edb, CustomPredicate.idb, CustomPredicate.idb_query):
            self.assertIsInstance(build("example", 1), CustomPredicate)

    def test_registration_still_owns_validation(self):
        with Engine() as engine:
            for build in (Predicate.edb, Predicate.idb, Predicate.idb_query):
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
