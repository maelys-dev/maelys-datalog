"""Former V1 behaviors retained, and intentional 0.10.0 contract changes.

Expected facts/text are independent of the binding implementation. Native-layout
and shim ABI tests disappear with the shim; result ownership replaces raw IDs.
"""
from concurrent.futures import ThreadPoolExecutor
import subprocess
import sys
import textwrap

import pytest

import maelys_datalog as md
from maelys_datalog import engine as binding


def graph(engine):
    engine.register_domain("python_migration_graph", [
        md.Predicate.edb("edge", 2), md.Predicate.edb("link", 2),
        md.Predicate.idb_query("path", 2), md.Predicate.idb_query("isolated", 1),
        md.Predicate.idb_query("reach", 2),
    ])
    return engine.load_inline_ruleset("python_migration_graph", "graph",
        "path(X,Y) :- edge(X,Y). path(X,Z) :- edge(X,Y),path(Y,Z). "
        "isolated(X) :- edge(X,X).")


def absent(result, predicate, values):
    assert result.explain_true(predicate, values) == (
        "MAELYS-DATALOG-v2\ndocument=why-true\nstatus=not-derived\nsteps=0 premises=0\n")


def test_single_package_has_public_statuses_and_no_native_symbol_api():
    assert md.Engine.__module__ == "maelys_datalog.engine"
    for status in md.Status:
        assert int(status) == getattr(binding.lib, "MAELYS_DATALOG_STATUS_" + status.name)
    for name in ("Term", "TERM_SYMBOL", "C", "DomainAlreadyRegisteredError",
                 "DomainRegistryFullError"):
        assert not hasattr(md, name)
    for name in ("intern_symbol", "symbol_text"):
        assert not hasattr(md.Ruleset, name)
    assert not hasattr(md.SolveResult, "explain_fact_text")
    assert not hasattr(md.SolveResult, "enumerate_predicate_facts_raw")


def test_graph_answers_and_canonical_explanation():
    with md.Engine() as engine:
        rules = graph(engine)
        edb = rules.edb()
        edb.add_facts([("edge", ["a", "b"]), ("edge", ["b", "c"])])
        with rules.solve(edb) as result:
            assert result.derived_fact_count() == 3
            assert set(result.enumerate_predicate_facts("path", 2)) == {
                ("a", "b"), ("b", "c"), ("a", "c")}
            assert result.enumerate_predicate_facts("isolated", 1) == []
            expected = (
                "MAELYS-DATALOG-v2\ndocument=why-true\nstatus=complete\n"
                'steps=1 premises=1\nstep=0 rule=1 fact="path"("a","b")\n'
                'premise=0 body=0 kind=positive origin=edb '
                'fact="edge"("a","b") parent=-\nresult-step=0\n')
            assert result.explain_true("path", ["a", "b"]) == expected
            assert result.explain_true("path", ["a", "b"]) == expected
            absent(result, "path", ["c", "a"])


def test_ground_queries_cover_all_origins_but_enumeration_is_idb_only():
    with md.Engine() as engine:
        domain = "python_migration_origins"
        engine.register_domain(domain, [
            md.Predicate.policy_fact_query("policy_enabled", 1),
            md.Predicate.edb_query("observed", 1), md.Predicate.idb_query("derived", 1),
            md.Predicate.edb("access", 2), md.Predicate.idb_query("allow", 2),
        ])
        rules = engine.load_inline_ruleset(domain, "origins",
            "policy_enabled(true). derived(X) :- observed(X). allow(U,D) :- access(U,D).")
        edb = rules.edb()
        edb.add_facts([("observed", ["alice"]), ("access", ["alice", "roadmap.pdf"]),
                      ("access", ['café\n"', "δ"])])
        with rules.solve(edb) as result:
            assert result.contains_fact("policy_enabled", [True])
            assert not result.contains_fact("policy_enabled", [1])
            for name in ("observed", "derived"):
                assert result.contains_fact(name, ["alice"])
            for name, terms in (("observed", ["alice"]), ("policy_enabled", [True])):
                assert result.enumerate_predicate_facts(name, 1) == []
                absent(result, name, terms)
            assert result.enumerate_predicate_facts("derived", 1) == [("alice",)]
            text = result.explain_true("allow", ['café\n"', "δ"])
            assert '"café\\n\\""' in text and '"δ"' in text
            before = result.enumerate_raw("allow", 2)
            assert not result.contains_fact("allow", ["unknown", "roadmap.pdf"])
            with pytest.raises(md.MaelysDatalogError) as error:
                result.explain_true("allow", ["unknown", "roadmap.pdf"])
            assert error.value.status == md.Status.NOT_FOUND
            assert result.enumerate_raw("allow", 2) == before
            # Predicate validation still precedes unknown-symbol absence.
            for operation in (result.contains_fact, result.explain_true):
                for name, values in (("missing", ["unknown"]), ("access", ["unknown", "x"]),
                                     ("allow", ["unknown"])):
                    with pytest.raises(md.MaelysDatalogError) as error:
                        operation(name, values)
                    assert error.value.status == md.Status.INVALID_FIELD


def test_or_explanation_matches_manual_expansion():
    with md.Engine() as engine:
        graph(engine).close()
        left = engine.load_inline_ruleset("python_migration_graph", "or",
            "path(X,Y) :- edge(X,Y) or link(X,Y).")
        right = engine.load_inline_ruleset("python_migration_graph", "manual",
            "path(X,Y) :- edge(X,Y). path(X,Y) :- link(X,Y).")
        texts = []
        for rules in (left, right):
            edb = rules.edb()
            edb.add_fact("link", ["alice", "doc"])
            with rules.solve(edb) as result:
                texts.append(result.explain_true("path", ["alice", "doc"]))
        assert texts[0] == texts[1]


def test_truncated_explanation_is_never_absence():
    with md.Engine() as engine:
        graph(engine).close()
        rules = engine.load_inline_ruleset("python_migration_graph", "truncated",
            "path(X,Y) :- edge(X,Y). path(X,Z) :- path(X,Y),edge(Y,Z). "
            "reach(X,Y) :- path(X,Y).")
        edb = rules.edb()
        edb.add_facts(("edge", [f"n{i}", f"n{i+1}"]) for i in range(8))
        with rules.solve(edb) as result:
            assert result.contains_fact("path", ["n0", "n8"])
            assert result.explain_true("path", ["n0", "n8"]) == (
                "MAELYS-DATALOG-v2\ndocument=why-true\nstatus=truncated\nsteps=0 premises=0\n")
            absent(result, "path", ["n8", "n0"])


def test_raw_enumeration_does_not_resolve_symbols_and_rejects_raw_input(monkeypatch):
    with md.Engine() as engine:
        rules = graph(engine)
        edb = rules.edb()
        edb.add_fact("edge", ["a", "b"])
        with rules.solve(edb) as result:
            raw = result.enumerate_raw("path", 2)
            assert result.enumerate_predicate_facts("path", 2) == [("a", "b")]
            assert tuple(term.resolve() for term in raw[0]) == ("a", "b")
            def no_resolve(_term):
                raise AssertionError("raw enumeration must not resolve symbols")
            monkeypatch.setattr(result, "resolve_term", no_resolve)
            assert result.enumerate_raw("path", 2) == raw
            with pytest.raises(TypeError):
                result.contains_fact("path", raw[0])
            with pytest.raises(TypeError):
                rules.edb().add_fact("edge", raw[0])
            with pytest.raises(TypeError):
                result.enumerate_raw("path", True)
            with pytest.raises(ValueError):
                result.enumerate_raw("path", engine.limits.max_arity + 1)


def test_invalid_inputs_fail_without_partial_append():
    with md.Engine() as engine:
        rules = graph(engine)
        edb = rules.edb()
        with rules.solve(edb) as result:
            for invalid in ("ab", b"ab", [object(), "b"], [1.5, "b"]):
                # A solved EDB needs an explicit reset before further appends.
                edb.reset()
                for operation, predicate in ((edb.add_fact, "edge"),
                                             (result.contains_fact, "path"),
                                             (result.explain_true, "path")):
                    with pytest.raises(TypeError):
                        operation(predicate, invalid)
            for operation in (edb.add_fact, result.contains_fact, result.explain_true):
                with pytest.raises(TypeError):
                    operation(123, ["a", "b"])
                with pytest.raises(ValueError):
                    operation("path", [0] * (engine.limits.max_arity + 1))
            assert len(edb) == 0


def test_domain_reuse_conflict_and_failed_load_recovery():
    with md.Engine() as engine:
        for _ in range(1000):
            graph(engine).close()
        with pytest.raises(md.MaelysDatalogError) as error:
            engine.register_domain("python_migration_graph", [md.Predicate.edb("other", 1)])
        assert error.value.status == md.Status.INVALID_FIELD
        with pytest.raises(md.MaelysDatalogError) as error:
            engine.load_inline_ruleset("python_migration_graph", "bad", "path(X,Y) :- missing(X,Y).")
        assert error.value.diagnostic.message
        with graph(engine) as rules:
            with rules.solve(rules.edb()) as result:
                assert result.derived_fact_count() == 0


def test_domain_registration_from_independent_threads():
    def worker(index):
        with md.Engine() as engine:
            engine.register_domain(f"python_migration_thread_{index}", [md.Predicate.edb("edge", 2)])
    with ThreadPoolExecutor(max_workers=2) as pool:
        list(pool.map(worker, range(2)))


def test_domain_saturation_isolated_from_other_tests():
    program = textwrap.dedent('''
        import maelys_datalog as md
        with md.Engine() as engine:
            for i in range(64):
                try:
                    engine.register_domain(f"python_saturation_{i}", [md.Predicate.edb("edge", 2)])
                except md.MaelysDatalogError as error:
                    assert error.status == md.Status.PAYLOAD_TOO_LARGE, error
                    break
            else:
                raise AssertionError("registry did not reach its declared bound")
    ''')
    child = subprocess.run([sys.executable, "-c", program], capture_output=True, text=True)
    assert child.returncode == 0, child.stdout + child.stderr
