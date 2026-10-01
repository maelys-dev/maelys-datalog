"""Retained-input transactions against the separately installed C SDK."""
import random
import unittest
from contextlib import closing
from maelys_datalog import (Engine, Predicate, InputBase, SessionInputs,
                            SessionCapacities, MaelysDatalogError, Status)


class SessionInputsTest(unittest.TestCase):
    def setUp(self):
        self.engine = Engine()
        self.addCleanup(self.engine.close)
        self.engine.register_domain("next_prepared", [Predicate.edb("seed", 1),
            Predicate.edb("blocked", 1), Predicate.idb_query("allow", 1)])
        self.rules = self.engine.load_inline_ruleset("next_prepared", "main",
            "allow(X) :- seed(X), not(blocked(X)).")

    def test_exact_values_set_semantics_and_stale_base(self):
        with self.rules.prepare(explanations=3) as session:
            fingerprint = session.execution_fingerprint
            with session.inputs(fact_capacity=16, symbols=["", "1", "é🙂"]) as inputs:
                self.assertNotEqual(session.execution_fingerprint, fingerprint)
                base = inputs.base
                values = [-(1 << 63), (1 << 63)-1, 9007199254740993, True, False, "", "1", "é🙂"]
                with inputs.replace(base, [("seed", [v]) for v in values]) as result:
                    for v in values:
                        self.assertTrue(result.contains_fact("allow", [v]))
                    self.assertEqual(result.derived_fact_count(), len(values))
                    self.assertIn("status=complete", result.explain_true("allow", ["é🙂"]))
                    with self.assertRaises(MaelysDatalogError):
                        inputs.close()
                    with self.assertRaises(RuntimeError):
                        inputs.apply(inputs.base)
                self.assertEqual(inputs.base, InputBase(base.incarnation, base.generation+1))
                with self.assertRaises(MaelysDatalogError) as error:
                    inputs.apply(base)
                self.assertEqual(error.exception.status, Status.INVALID_STATE)
                current = inputs.base
                for forged in (InputBase(current.incarnation, current.generation + (1 << 32)),
                               InputBase(current.incarnation + (1 << 32), current.generation)):
                    with self.assertRaises(MaelysDatalogError): inputs.apply(forged)
                    self.assertEqual(inputs.base, current)
                with inputs.apply(inputs.base, added=[("seed", ["é🙂"])]*2,
                                  removed=[("seed", ["é🙂"]), ("seed", ["unknown"])]) as result:
                    self.assertTrue(result.contains_fact("allow", ["é🙂"]))
                with inputs.apply(inputs.base, added=[("blocked", ["é🙂"])]) as result:
                    self.assertFalse(result.contains_fact("allow", ["é🙂"]))
                with inputs.apply(inputs.base, removed=[("blocked", ["é🙂"])]) as result:
                    self.assertTrue(result.contains_fact("allow", ["é🙂"]))
            self.assertEqual(session.execution_fingerprint, fingerprint)

    def test_atomic_conversion_vocabulary_domain_capacity_and_reuse(self):
        with self.rules.prepare(capacities=SessionCapacities(input_facts=2)) as session:
            inputs = session.inputs(addition_capacity=3, removal_capacity=3, symbols=["known"])
            with inputs.replace(inputs.base, [("seed", [1])]): pass
            base = inputs.base
            cases = [dict(added=[("seed", [2]), ("seed", [object()])]),
                     dict(added=[("seed", [2])], removed=[("seed", [object()])]),
                     dict(added=[("seed", ["unknown"])]),
                     dict(added=[("seed", [2]), ("seed", [3])]),
                     dict(added=[("missing", [1])]),
                     dict(removed=[("missing", ["unknown"])])]
            for changes in cases:
                with self.assertRaises((TypeError, MaelysDatalogError)):
                    inputs.apply(base, **changes)
                self.assertEqual(inputs.base, base)
            # Raw bounds apply before duplicate elimination.
            with self.assertRaises(MaelysDatalogError): inputs.apply(base, added=[("seed", [1])]*4)
            with inputs.apply(base, added=[("seed", [2])], removed=[("seed", [1])]) as r:
                self.assertFalse(r.contains_fact("allow", [1]))
                self.assertTrue(r.contains_fact("allow", [2]))

    def test_reentrancy_and_bounded_iterators(self):
        with self.rules.prepare() as session:
            def vocabulary():
                session.close()
                yield "unused"
            with self.assertRaises(RuntimeError): session.inputs(symbols=vocabulary())
            inputs = session.inputs(fact_capacity=2)
            base = inputs.base
            for action in (session.close, inputs.close, lambda: inputs.apply(base)):
                def facts():
                    yield ("seed", [1])
                    action()
                with self.assertRaises(RuntimeError): inputs.replace(base, facts())
                self.assertEqual(inputs.base, base)
            consumed = []
            def infinite():
                while True:
                    consumed.append(1)
                    yield ("seed", [1])
            with self.assertRaises(MaelysDatalogError): inputs.replace(base, infinite())
            self.assertEqual(len(consumed), 3)
            with inputs.apply(base) as r: self.assertEqual(r.derived_fact_count(), 0)

    def test_attachment_ownership_incarnation_and_cascading_close(self):
        with self.assertRaises(TypeError): SessionInputs(None, None, None, None, ())
        session = self.rules.prepare()
        inputs = session.inputs(fact_capacity=1)
        old = inputs.base
        with self.assertRaises(RuntimeError): session.inputs()
        with closing(self.rules.edb()) as edb:
            with self.assertRaises(MaelysDatalogError): session.solve(edb)
        inputs.close()
        other = session.inputs(fact_capacity=1)
        self.assertNotEqual(other.base.incarnation, old.incarnation)
        with self.assertRaises(MaelysDatalogError): other.apply(old)
        result = other.replace(other.base, [("seed", [1])])
        session.close()
        result.close(); other.close(); session.close()
        with self.assertRaises(RuntimeError): _ = other.base
        with self.rules.prepare() as solved, closing(self.rules.edb()) as edb:
            with solved.solve(edb): pass
            with self.assertRaises(MaelysDatalogError): solved.inputs()

    def test_zero_options_and_exact_base_validation(self):
        for value in (True, 1.5, "1"):
            with self.assertRaises(TypeError): InputBase(value, 0)
        for value in (-1, 1 << 64):
            with self.assertRaises(ValueError): InputBase(1, value)
        self.assertEqual(InputBase((1 << 64)-1, (1 << 64)-1).generation, (1 << 64)-1)
        with self.rules.prepare() as session:
            for value in (-1, True, 1.5, 1 << 128):
                with self.assertRaises((TypeError, ValueError)): session.inputs(fact_capacity=value)
            with session.inputs(fact_capacity=0) as inputs:
                with inputs.replace(inputs.base, []): pass
                with self.assertRaises(MaelysDatalogError): inputs.apply(inputs.base, added=[("seed", [1])])

    def test_generated_deltas_against_complete_snapshot_oracle(self):
        rng = random.Random(170017)
        with self.rules.prepare() as session, self.rules.prepare() as oracle, closing(self.rules.edb()) as edb:
            inputs = session.inputs(fact_capacity=32, addition_capacity=16, removal_capacity=16)
            committed = set()
            for turn in range(80):
                added = {(rng.choice(("seed", "blocked")), rng.randrange(12)) for _ in range(7)}
                removed = {(rng.choice(("seed", "blocked")), rng.randrange(12)) for _ in range(7)}
                committed = (committed - removed) | added
                if turn % 9 == 0:
                    candidate = inputs.replace(inputs.base, [(p, [v]) for p,v in sorted(committed)])
                else:
                    candidate = inputs.apply(inputs.base, added=[(p,[v]) for p,v in sorted(added)],
                                              removed=[(p,[v]) for p,v in sorted(removed)])
                edb.reset(); edb.add_facts((p,[v]) for p,v in sorted(committed))
                with candidate, oracle.solve(edb) as reference:
                    self.assertEqual(candidate.enumerate_predicate_facts("allow", 1),
                                     reference.enumerate_predicate_facts("allow", 1))

    def test_solver_rejection_preserves_diagnostics_base_and_facts(self):
        self.engine.register_domain("binding_sums", [Predicate.edb("ev", 2), Predicate.idb_query("total", 1)])
        rules = self.engine.load_inline_ruleset("binding_sums", "main", "total(N) :- sum(V,ev(_,V),N).")
        with rules.prepare() as session, closing(rules.edb()) as edb:
            inputs = session.inputs(fact_capacity=4)
            with inputs.replace(inputs.base, [("ev", [1,2147483647])]): pass
            base = inputs.base
            for value in (1, -1):
                edb.reset(); edb.add_facts([("ev", [1,2147483647]), ("ev", [2,value])])
                with self.assertRaises(MaelysDatalogError) as reference: rules.solve(edb)
                with self.assertRaises(MaelysDatalogError) as candidate:
                    inputs.apply(base, added=[("ev", [2,value])])
                self.assertEqual(candidate.exception.diagnostic, reference.exception.diagnostic)
                self.assertEqual(inputs.base, base)
            with inputs.apply(base, added=[("ev", [2,1])], removed=[("ev", [1,2147483647])]) as r:
                self.assertTrue(r.contains_fact("total", [1]))
