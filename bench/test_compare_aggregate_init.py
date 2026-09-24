# SPDX-License-Identifier: MPL-2.0
import copy
import unittest
from aggregate_init_proof import TARGET, difference, parse
class Proof(unittest.TestCase):
    def test_one_store_per_call(self):
        a={TARGET:dict(Ir=100,Dr=20,Dw=30),'other':dict(Ir=4,Dr=3,Dw=2)}
        b=copy.deepcopy(a); b[TARGET].update(Ir=106,Dw=33)
        self.assertEqual(difference(a,b,3),dict(Ir=6,Dr=0,Dw=3))
        for function,event in [(TARGET,'Dw'),(TARGET,'Dr'),('other','Ir'),('other','Dw')]:
            m=copy.deepcopy(b);m[function][event]+=1
            with self.assertRaises(ValueError):difference(a,m,3)
    def test_zero_call_edges_are_not_self_costs(self):
        text='''desc: Trigger: Client Request: sum/sorted/8
positions: line
events: Ir Dr Dw
summary: 12 5 4
fn=(1) caller
1 2 1 1
cfn=(2) solve_aggregate_literal
calls=1 10
2 10 4 3
fn=(2)
10 10 4 3
cfn=(3) startup
calls=0 1
11 999 999 999
'''
        key,costs,calls=parse(text)
        self.assertEqual(key,('sum','sorted','8'));self.assertEqual(calls,1)
        self.assertEqual(costs[TARGET],dict(Ir=10,Dr=4,Dw=3))
        self.assertEqual(costs['<unattributed>'],dict(Ir=0,Dr=0,Dw=0))
