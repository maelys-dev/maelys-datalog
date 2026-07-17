from __future__ import annotations

import os
import subprocess
import sys
import textwrap
import threading

import pytest

import maelys_datalog as md


def predicates():
    return [
        md.Predicate("edge", 2, md.PRED_EDB),
        md.Predicate("path", 2, md.PRED_IDB | md.PRED_QUERY),
        md.Predicate("isolated", 1, md.PRED_IDB | md.PRED_QUERY),
    ]


def make_ruleset(engine: md.Engine, domain: str = "py_test_graph"):
    engine.register_domain(domain, predicates())
    return engine.load_inline_ruleset(
        domain,
        "policy",
        "path(X, Y) :- edge(X, Y).\n"
        "path(X, Z) :- edge(X, Y), path(Y, Z).\n"
        "isolated(X) :- edge(X, X).",
    )


def test_import_build_limits_and_abi_constants():
    with md.Engine() as engine:
        assert engine.limits.max_symbols == 512
        assert engine.limits.max_facts_per_pred in (64, 256)
    assert md.TERM_SYMBOL != md.TERM_INT
    assert md.PRED_QUERY != 0


def test_register_load_solve_enumerate_and_symbol_resolution():
    with md.Engine() as engine:
        ruleset = make_ruleset(engine, "py_test_graph_solve")
        edb = ruleset.edb()
        edb.add_fact("edge", ["a", "b"])
        edb.add_fact("edge", ["b", "c"])
        result = ruleset.solve(edb)
        assert result.derived_fact_count() == 3
        assert set(result.enumerate_predicate_facts("path", 2)) == {
            ("a", "b"),
            ("b", "c"),
            ("a", "c"),
        }
        assert result.enumerate_predicate_facts("isolated", 1) == []
        result.close()
        edb.close()
        ruleset.close()


def test_load_failure_reports_engine_diag_before_ruleset_exists():
    with md.Engine() as engine:
        engine.register_domain("py_test_bad_load", predicates())
        with pytest.raises(md.MaelysDatalogError) as exc:
            engine.load_inline_ruleset(
                "py_test_bad_load",
                "bad",
                "path(X, Y) :- missing(X, Y).",
            )
        assert exc.value.code != md.C.OK


def test_edb_is_closed_for_mutation_after_solve_and_second_solve_is_defined():
    with md.Engine() as engine:
        ruleset = make_ruleset(engine, "py_test_edb_closed")
        edb = ruleset.edb()
        edb.add_fact("edge", ["a", "b"])
        first = ruleset.solve(edb)
        assert first.derived_fact_count() == 1
        with pytest.raises(RuntimeError):
            edb.add_fact("edge", ["b", "c"])
        second = ruleset.solve(edb)
        assert second.derived_fact_count() == 1
        first.close()
        second.close()
        edb.close()
        ruleset.close()


def test_parent_close_closes_children():
    engine = md.Engine()
    ruleset = make_ruleset(engine, "py_test_close_cascade")
    edb = ruleset.edb()
    edb.add_fact("edge", ["a", "b"])
    result = ruleset.solve(edb)
    engine.close()
    with pytest.raises(RuntimeError):
        ruleset.edb()
    with pytest.raises(RuntimeError):
        edb.add_fact("edge", ["b", "c"])
    with pytest.raises(RuntimeError):
        result.derived_fact_count()


def test_existing_domain_with_different_predicates_fails_closed():
    with md.Engine() as engine:
        engine.register_domain("py_test_conflict", predicates())
        with pytest.raises(md.DomainAlreadyRegisteredError):
            engine.register_domain(
                "py_test_conflict",
                [md.Predicate("edge", 2, md.PRED_EDB)],
            )


def test_domain_registry_lock_allows_concurrent_distinct_registers():
    errors = []

    def worker(index: int):
        try:
            with md.Engine() as engine:
                engine.register_domain(f"py_thread_domain_{index}", predicates())
        except BaseException as exc:  # pragma: no cover - surfaced below
            errors.append(exc)

    threads = [threading.Thread(target=worker, args=(i,)) for i in range(2)]
    for thread in threads:
        thread.start()
    for thread in threads:
        thread.join()
    assert errors == []


def test_domain_saturation_runs_in_subprocess(tmp_path):
    code = textwrap.dedent(
        """
        import maelys_datalog as md
        preds = [md.Predicate("edge", 2, md.PRED_EDB)]
        with md.Engine() as engine:
            saw_full = False
            for i in range(32):
                try:
                    engine.register_domain(f"py_saturation_{i}", preds)
                except md.DomainRegistryFullError:
                    saw_full = True
                    break
            assert saw_full
        """
    )
    env = os.environ.copy()
    package_root = os.path.abspath(
        os.path.join(os.path.dirname(__file__), "..", "..", "bindings", "python")
    )
    env["PYTHONPATH"] = package_root + os.pathsep + env.get("PYTHONPATH", "")
    completed = subprocess.run(
        [sys.executable, "-c", code],
        cwd=str(tmp_path),
        env=env,
        text=True,
        capture_output=True,
        check=False,
    )
    assert completed.returncode == 0, completed.stderr + completed.stdout
