"""Full source variable names through an installed Python SDK."""
import subprocess
import sys
import unittest
from maelys_datalog import Engine, Predicate, PRED_EDB, PRED_IDB, PRED_QUERY


class VariableNamesTest(unittest.TestCase):
    def test_variable_names(self):
        # Keep the process-wide bounded registry independent of other tests.
        if __name__ != "__main__":
            child = subprocess.run([sys.executable, __file__], capture_output=True, text=True)
            self.assertEqual(child.returncode, 0, child.stdout + child.stderr)
            return
        with Engine() as engine:
            engine.register_domain("variable_names", [
                Predicate("edge", 2, PRED_EDB), Predicate("seed", 2, PRED_EDB),
                Predicate("out", 2, PRED_IDB | PRED_QUERY),
                Predicate("log", 3, PRED_EDB), Predicate("service", 1, PRED_EDB),
                Predicate("errors", 2, PRED_IDB),
                Predicate("error_count", 2, PRED_IDB | PRED_QUERY),
                Predicate("alert", 1, PRED_IDB | PRED_QUERY)], atoms=["error"])
            cases = [
                ("out(U,Ux) :- edge(U,Ux), Ux > U.", {(1, 2), (2, 3)}),
                ("out(U,G2) :- out(U,G1), edge(G1,G2). out(U,G) :- seed(U,G).",
                 {(7, 1), (7, 2), (7, 3)}),
                ("out(Id,Service) :- edge(Id,Service), Service = Id + 1.", {(1, 2), (2, 3)}),
            ]
            for source, expected in cases:
                with self.subTest(source=source):
                    rules = engine.load_inline_ruleset("variable_names", "names", source)
                    edb = rules.edb()
                    edb.add_facts([("edge", [1, 2]), ("edge", [2, 3]), ("seed", [7, 1])])
                    session = rules.prepare()
                    result = session.solve(edb)
                    self.assertEqual(set(result.enumerate_predicate_facts("out", 2)), expected)
                    result.close(); session.close(); edb.close(); rules.close()

            # README's complete aggregate example, with distinct Id and Service.
            rules = engine.load_inline_ruleset("variable_names", "readme",
                'errors(Id, Service) :- log(Id, Service, "error"). '
                'error_count(Service, N) :- service(Service), count(Id, errors(Id, Service), N). '
                'alert(Service) :- error_count(Service, N), N >= 10.')
            edb = rules.edb()
            edb.add_facts([("service", ["api"]), ("service", ["empty"])] +
                          [("log", [i, "api", "error"]) for i in range(10)])
            session = rules.prepare()
            result = session.solve(edb)
            self.assertEqual(set(result.enumerate_predicate_facts("error_count", 2)),
                             {("api", 10), ("empty", 0)})
            self.assertEqual(result.enumerate_predicate_facts("alert", 1), [("api",)])
            result.close(); session.close(); edb.close(); rules.close()


if __name__ == "__main__":
    unittest.main()
