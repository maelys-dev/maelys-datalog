"""Fixed quotas are distinct from build bounds and Python input storage."""
import unittest
import hashlib
from contextlib import closing

from maelys_datalog import (
    Engine, Predicate, PRED_EDB, PRED_IDB, PRED_QUERY,
    MaelysDatalogError, SessionCapacities, Status,
)


class SessionResourcesTest(unittest.TestCase):
    def setUp(self):
        self.engine = Engine()
        self.addCleanup(self.engine.close)
        self.engine.register_domain("fixed_resources", [
            Predicate("seed", 1, PRED_EDB), Predicate("aux", 1, PRED_IDB),
            Predicate("seen", 1, PRED_IDB | PRED_QUERY),
        ])
        self.rules = self.engine.load_inline_ruleset(
            "fixed_resources", "fixed", "aux(X) :- seed(X). seen(X) :- aux(X).")

    def test_elastic_allocation_not_exposed(self):
        for name, value in (("memory_mode", 1), ("allocator", object()),
                            ("execution_byte_cap", 1024)):
            with self.assertRaises(TypeError):
                SessionCapacities(**{name: value})
            with self.assertRaises(TypeError):
                self.rules.prepare(**{name: value})

    def test_defaults_and_effective_identity(self):
        with self.rules.prepare() as default:
            fingerprint = default.execution_fingerprint
            effective = default.capacities
        for request in (SessionCapacities(), effective):
            with self.rules.prepare(capacities=request) as session:
                self.assertEqual(session.capacities, effective)
                self.assertEqual(session.execution_fingerprint, fingerprint)
        with self.rules.prepare(capacities=SessionCapacities(input_facts=2)) as small:
            self.assertEqual(small.capacities.input_facts, 2)
            self.assertEqual(small.capacities.derived_facts, effective.derived_facts)
            self.assertNotEqual(small.execution_fingerprint, fingerprint)
            q = small.capacities
            encoded = (f"maelys-execution-v2\n{fingerprint}\n{q.input_facts}\n"
                       f"{q.derived_facts}\n{q.symbols}\n{q.text_bytes}\n0\n1\n").encode("ascii")
            self.assertEqual(small.execution_fingerprint, hashlib.sha256(encoded).hexdigest())

    def test_capacity_failure_is_atomic_and_reusable(self):
        with self.rules.prepare(capacities=SessionCapacities(2, 2, 1, 3)) as session:
            with closing(self.rules.edb()) as edb:
                for facts, field, bound in (
                    ([("seed", [1]), ("seed", [2]), ("seed", [1])], "session_input_facts", 2),
                    ([("seed", [1]), ("seed", [2])], "session_derived_facts", 2),
                    ([("seed", ["a"]), ("seed", ["b"])], "session_symbols", 1),
                    ([("seed", ["aaa"])], "session_text_bytes", 3),
                ):
                    edb.reset()
                    edb.add_facts(facts)
                    with self.assertRaises(MaelysDatalogError) as raised:
                        session.solve(edb)
                    self.assertEqual(raised.exception.status, Status.PAYLOAD_TOO_LARGE)
                    self.assertEqual(raised.exception.diagnostic.field, field)
                    self.assertEqual(raised.exception.diagnostic.limit, bound)
                    edb.reset()
                    edb.add_facts([("seed", [7])])
                    with session.solve(edb) as result:
                        self.assertTrue(result.contains_fact("seen", [7]))

    def test_zero_and_convenience(self):
        with closing(self.rules.edb()) as edb:
            with self.rules.solve(edb, capacities=SessionCapacities(0, 0, 0, 0)):
                pass
            edb.reset()
            edb.add_facts([("seed", [1])])
            with self.assertRaises(MaelysDatalogError):
                self.rules.solve(edb, capacities=SessionCapacities(input_facts=0))
            with self.rules.solve(edb, capacities=SessionCapacities(1, 2, 0, 0)) as result:
                self.assertTrue(result.contains_fact("seen", [1]))

    def test_validation_never_clamps(self):
        for bad in (True, 1.5, "8"):
            with self.assertRaises(TypeError):
                SessionCapacities(input_facts=bad)
        for bad in (-1, 1 << 128):
            with self.assertRaises(ValueError):
                SessionCapacities(text_bytes=bad)
        with self.assertRaises(TypeError):
            self.rules.prepare(capacities={"input_facts": 1})
        with self.rules.prepare() as default:
            ceiling = default.capacities.input_facts
        with self.assertRaises(MaelysDatalogError) as raised:
            self.rules.prepare(capacities=SessionCapacities(input_facts=ceiling + 1))
        self.assertEqual(raised.exception.status, Status.UNSUPPORTED)
