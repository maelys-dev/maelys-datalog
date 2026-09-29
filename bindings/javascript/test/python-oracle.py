"""Independent Python execution of the shared input, not a JS-generated answer."""
import json
import sys
from maelys_datalog import Engine, Predicate
case = json.load(sys.stdin)
with Engine() as engine:
    engine.register_domain(case['domain'], [Predicate(p['name'], p['arity'], p['flags']) for p in case['predicates']], atoms=case.get('atoms', []))
    with engine.load_inline_ruleset(case['domain'], 'main', case['source']) as rules:
        edb = rules.edb()
        try:
            edb.add_facts((f['predicate'], f['terms']) for f in case['facts'])
            with rules.solve(edb, explanations=case.get('explanations', 0)) as result:
                print(json.dumps({
                    'answers': [result.contains_fact(q['predicate'], q['terms']) for q in case['queries']],
                    'documents': [getattr(result, 'explain_' + q['kind'])(q['predicate'], q['terms']) for q in case['documents']],
                    'policy': rules.fingerprint,
                    'execution': result.execution_fingerprint,
                }))
        finally:
            edb.close()
