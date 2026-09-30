"""Build bounds, normalized program counts and effective quotas stay distinct."""
import unittest
from dataclasses import FrozenInstanceError
from maelys_datalog import Engine, Predicate, ProgramCounts, SessionCapacities


class ConsumerIntrospectionTest(unittest.TestCase):
    def test_counts_and_quotas(self):
        with Engine() as engine:
            self.assertEqual(engine.limits.max_policy_atoms, 256)
            self.assertEqual(engine.limits.max_policy_atom_bytes, 63)
            engine.register_domain("consumer_counts", [Predicate.edb("seed", 1),
                Predicate.policy_fact("fixed", 1), Predicate.idb_query("out", 1),
                Predicate.edb("unused", 1)])
            with engine.load_inline_ruleset("consumer_counts", "counts",
                    "fixed(3). out(X) :- seed(X) or fixed(X).") as rules:
                counts = rules.program_counts()
                self.assertEqual(counts, ProgramCounts(4, 1, 2))
                with self.assertRaises(FrozenInstanceError):
                    counts.rules = 99
                fingerprint = rules.fingerprint
                with rules.prepare(capacities=SessionCapacities(input_facts=0)) as session:
                    self.assertEqual(session.capacities.input_facts, 0)
                    self.assertEqual(rules.program_counts(), counts)
                    self.assertEqual(rules.fingerprint, fingerprint)
                for index in (-1, 1, 1 << 100):
                    with self.assertRaises(IndexError):
                        rules.program_counts(index)
                for index in (True, 0.0, "0"):
                    with self.assertRaises(TypeError):
                        rules.program_counts(index)
            with self.assertRaises(RuntimeError):
                rules.program_counts()
