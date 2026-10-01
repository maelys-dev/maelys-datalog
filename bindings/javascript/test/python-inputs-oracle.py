"""Independent retained-input trace, exact values and diagnostics (SDK installed)."""
import json
from maelys_datalog import Engine, Predicate, MaelysDatalogError
facts = lambda entries: [(p,[v]) for p,v in entries]
with Engine() as engine:
    engine.register_domain("js_inputs_parity", [Predicate.edb("seed",1), Predicate.edb("blocked",1), Predicate.idb_query("allow",1)])
    rules=engine.load_inline_ruleset("js_inputs_parity","main","allow(X) :- seed(X), not(blocked(X)).")
    session=rules.prepare(explanations=3)
    inputs=session.inputs(fact_capacity=8, addition_capacity=4, removal_capacity=4, symbols=["alice", "bob"])
    trace=[]
    def record(result):
        with result:
            trace.append(dict(generation=str(inputs.base.generation),
                answers=[result.contains_fact("allow",[v]) for v in ["alice","bob",(1<<63)-1]],
                explanation=result.explain_true("allow",["alice"]),
                execution=result.execution_fingerprint))
    initial=inputs.base
    record(inputs.replace(initial,facts([("seed","alice"),("seed","bob"),("seed",(1<<63)-1)])))
    record(inputs.apply(inputs.base, added=facts([("blocked","bob")]), removed=facts([("seed","never-interned")])))
    record(inputs.apply(inputs.base, removed=facts([("blocked","bob")]), added=facts([("seed","alice")])))
    errors=[]
    for base, added in [(initial,[]),(inputs.base,[("seed","unknown")]),(inputs.base,[("missing",1)])]:
        try: inputs.apply(base,added=facts(added))
        except MaelysDatalogError as error:
            d=error.diagnostic
            errors.append(dict(status=error.status, present=str(d.present), code=d.code,
                               field=d.field, token=d.token, message=d.message, hint=d.hint,
                               generation=str(inputs.base.generation)))
        else: raise AssertionError("transaction should have failed")
    record(inputs.apply(inputs.base))
    print(json.dumps(dict(trace=trace,errors=errors)))
