"""Cached structural reads through the installed Python SDK."""
import subprocess
import sys
import unittest
from maelys_datalog import Engine, Predicate

class PolicyReadsTest(unittest.TestCase):
    def test_reads(self):
        if __name__ != '__main__':
            result = subprocess.run([sys.executable, __file__], capture_output=True, text=True)
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
            return
        with Engine() as engine:
            engine.register_domain('reads', [Predicate.edb('seed',1), Predicate.edb('blocked',1),
                Predicate.edb('agg',1), Predicate.edb('unused',1), Predicate.idb_query('out',1),
                Predicate.policy_fact('fixed',1)], atoms=['a'])
            for source, expected in [
                ('out(X):-seed(X),not(blocked(X)). fixed(1).', {'seed','blocked'}),
                ('out(X):-seed(X),X=X,starts_with(X,"a").', {'seed'}),
                *[(f'out(N):-{kind}(V,agg(V),N).', {'agg'}) for kind in ('count','min','max','sum')],
            ]:
                with engine.load_inline_ruleset('reads','reads',source) as rules:
                    for name in ('seed','blocked','agg','unused','out','fixed','unknown'):
                        self.assertEqual(rules.reads_predicate(name,1),name in expected)
                    self.assertFalse(rules.reads_predicate('seed',2))
                    with self.assertRaises(IndexError): rules.reads_predicate('seed',1,1)
                    with self.assertRaises(TypeError): rules.reads_predicate('seed',True)
                with self.assertRaises(RuntimeError): rules.reads_predicate('seed',1)

if __name__ == '__main__': unittest.main()
