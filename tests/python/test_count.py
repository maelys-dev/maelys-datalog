"""The existing Python binding accepts the same public count language."""
import maelys_datalog as md


def test_stratified_count_groups_and_explanation():
    with md.Engine() as engine:
        engine.register_domain("legacy_count", [
            md.Predicate("group", 1, md.PRED_EDB),
            md.Predicate("event", 3, md.PRED_EDB),
            md.Predicate("total", 2, md.PRED_IDB | md.PRED_QUERY),
        ])
        rules = engine.load_inline_ruleset("legacy_count", "count",
            "total(G,N) :- group(G),count(I,event(I,G,_),N).")
        edb = rules.edb()
        for predicate, terms in [("group", ["api"]), ("group", ["worker"]),
                                 ("event", [1, "api", "a"]), ("event", [1, "api", "b"]),
                                 ("event", [2, "api", "a"])]:
            edb.add_fact(predicate, terms)
        result = rules.solve(edb)
        assert set(result.enumerate_predicate_facts("total", 2)) == {("api", 2), ("worker", 0)}
        assert 'kind=count origin=edb' in result.explain_fact_text("total", ["api", 2])
        result.close()
        edb.close()
        rules.close()
