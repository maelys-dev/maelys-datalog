"""Count through the opaque facade, with session snapshots and explanations."""
import subprocess
import sys
import unittest
from maelys_datalog_next import Capability, Engine, Predicate, PRED_EDB, PRED_IDB, PRED_QUERY


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


if __name__ == "__main__":
    unittest.main()
