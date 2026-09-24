"""Count through the opaque facade, with session snapshots and explanations."""
import subprocess
import sys
import unittest
from maelys_datalog import MaelysDatalogError, Capability, Engine, Predicate, PRED_EDB, PRED_IDB, PRED_QUERY


class CountTest(unittest.TestCase):
    def test_groups_projection_zero_and_session_reuse(self):
        # The default registry is process-wide and bounded. Keep this new
        # domain independent of the existing suite's complete domain inventory.
        if __name__ != "__main__":
            child = subprocess.run([sys.executable, __file__], capture_output=True, text=True)
            self.assertEqual(child.returncode, 0, child.stdout + child.stderr)
            return
        with Engine() as engine:
            engine.register_domain("next_count", [
                Predicate("group", 1, PRED_EDB),
                Predicate("event", 3, PRED_EDB),
                Predicate("total", 2, PRED_IDB | PRED_QUERY),
            ])
            rules = engine.load_inline_ruleset("next_count", "count",
                "total(G,N) :- group(G),count(I,event(I,G,_),N).")
            prepared = rules.prepare(required_capabilities=Capability.AGGREGATES)
            edb = rules.edb()
            edb.add_facts([("group", ["api"]), ("group", ["worker"]),
                           ("event", [1, "api", "a"]), ("event", [1, "api", "b"]),
                           ("event", [2, "api", "a"])])
            result = prepared.solve(edb)
            self.assertEqual(set(result.enumerate_predicate_facts("total", 2)),
                             {("api", 2), ("worker", 0)})
            self.assertIn('kind=count origin=edb', result.explain_true("total", ["api", 2]))
            self.assertIn('observed=2 expected=3', result.explain_false("total", ["api", 3]))
            result.close()
            empty = rules.edb()
            empty.add_fact("group", ["api"])
            result = prepared.solve(empty)
            self.assertEqual(result.enumerate_predicate_facts("total", 2), [("api", 0)])
            result.close()
            empty.close()
            edb.close()
            prepared.close()
            rules.close()

            for op, cap, value in [("min", Capability.MIN, 7), ("max", Capability.MAX, 10), ("sum", Capability.SUM, 27)]:
                rules = engine.load_inline_ruleset("next_count", op,
                    f"total(G,N) :- group(G),{op}(V,event(_,G,V),N).")
                prepared = rules.prepare(required_capabilities=cap)
                edb = rules.edb()
                edb.add_facts([("group", ["api"]), ("group", ["worker"]),
                               ("event", [1, "api", 10]), ("event", [2, "api", 10]),
                               ("event", [3, "api", 7]), ("event", [1, "api", 10])])
                result = prepared.solve(edb)
                expected = {("api", value)} | ({("worker", 0)} if op == "sum" else set())
                self.assertEqual(set(result.enumerate_predicate_facts("total", 2)), expected)
                self.assertIn(f"kind={op} origin=edb", result.explain_true("total", ["api", value]))
                self.assertIn(f"{op}-mismatch", result.explain_false("total", ["api", 99]))
                if op != "sum":
                    self.assertIn(f"{op}-empty", result.explain_false("total", ["worker", 0]))
                result.close()
                edb.close()
                for bad in (-1, -(1 << 63), (1 << 63) - 1, 2147483648, True, "wrong"):
                    invalid = rules.edb()
                    invalid.add_facts([("group", ["api"]), ("event", [1, "api", bad])])
                    with self.assertRaises(MaelysDatalogError) as caught:
                        prepared.solve(invalid)
                    d = caught.exception.diagnostic
                    self.assertEqual(d.message, "aggregate_domain_error")
                    self.assertEqual((d.predicate, d.field, d.term_index, d.limit), ("event", op, 2, 2147483647))
                    self.assertEqual(d.token, "true" if bad is True else str(bad))
                    self.assertTrue(d.present & 256)
                    self.assertFalse(d.present & 4)
                    invalid.close()
                empty = rules.edb()
                result = prepared.solve(empty)
                result.close()
                empty.close()
                prepared.close()
                rules.close()


if __name__ == "__main__":
    unittest.main()
