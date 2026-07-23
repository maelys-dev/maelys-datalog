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
    assert md.InputTerm is not None
    assert md.ResolvedTerm is not None
    assert md.Fact is not None
    assert md.RawFact is not None


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


def test_python_validates_build_arity_before_native_registration():
    domain = "py_test_invalid_build_arity"
    with md.Engine() as engine:
        with pytest.raises(ValueError, match="outside supported range"):
            engine.register_domain(
                domain,
                [md.Predicate("too_wide", engine.limits.max_arity + 1, md.PRED_EDB)],
            )
        found, _inspectable, _predicates = engine._find_domain(domain)
        assert not found


def test_python_argument_taxonomy_and_explicit_symbol_validation():
    with md.Engine() as engine:
        ruleset = make_ruleset(engine, "py_test_argument_taxonomy")
        edb = ruleset.edb()

        for invalid_terms in ("alice", b"alice"):
            with pytest.raises(TypeError, match="sequence"):
                edb.add_fact("edge", invalid_terms)
        with pytest.raises(TypeError, match="predicate"):
            edb.add_fact(123, ["a", "b"])
        with pytest.raises(ValueError, match="outside supported range"):
            edb.add_fact(
                "edge",
                [0] * (engine.limits.max_arity + 1),
            )
        with pytest.raises(md.MaelysDatalogError) as exc:
            edb.add_fact(
                "edge",
                [md.Term.symbol_id(engine.limits.max_symbols), md.Term.integer(1)],
            )
        assert exc.value.code == md.C.ERR_INVALID_FIELD

        valid_symbol = ruleset.intern_symbol("known")
        edb.add_fact("edge", [md.Term.symbol_id(valid_symbol), "target"])
        ruleset.close()


def test_raw_enumeration_matches_resolved_and_never_resolves_symbols(monkeypatch):
    class RecordingRLock:
        def __init__(self):
            self._lock = threading.RLock()
            self.depth = 0
            self.events = []

        def __enter__(self):
            self._lock.acquire()
            self.depth += 1
            self.events.append(("enter", self.depth))
            return self

        def __exit__(self, *_exc):
            self.events.append(("exit", self.depth))
            self.depth -= 1
            self._lock.release()

    with md.Engine() as engine:
        ruleset = make_ruleset(engine, "py_test_raw_enumeration")
        edb = ruleset.edb()
        edb.add_fact("edge", ["a", "b"])
        result = ruleset.solve(edb)

        raw = result.enumerate_predicate_facts_raw("path", 2)
        recording_lock = RecordingRLock()
        ruleset._symbol_lock = recording_lock
        resolved = result.enumerate_predicate_facts("path", 2)
        assert len(raw) == len(resolved) == 1
        assert resolved == [("a", "b")]
        assert all(isinstance(term, md.Term) for term in raw[0])
        assert recording_lock.events[0] == ("enter", 1)
        assert recording_lock.events[-1] == ("exit", 1)
        assert ("exit", 1) not in recording_lock.events[:-1]
        assert max(depth for _event, depth in recording_lock.events) == 2

        def fail_symbol_text(_symbol_id):
            raise AssertionError("raw enumeration must not resolve symbols")

        monkeypatch.setattr(ruleset, "symbol_text", fail_symbol_text)
        assert result.enumerate_predicate_facts_raw("path", 2) == raw

        with pytest.raises(TypeError, match="arity"):
            result.enumerate_predicate_facts_raw("path", True)
        with pytest.raises(ValueError, match="outside supported range"):
            result.enumerate_predicate_facts(
                "path", engine.limits.max_arity + 1
            )


def test_distinct_edbs_serialize_symbol_updates_then_solve_sequentially():
    predicates = [
        md.Predicate("seen", 2, md.PRED_EDB),
        md.Predicate("out", 2, md.PRED_IDB | md.PRED_QUERY),
    ]
    with md.Engine() as engine:
        engine.register_domain("py_test_symbol_lock", predicates)
        ruleset = engine.load_inline_ruleset(
            "py_test_symbol_lock",
            "policy",
            "out(T, S) :- seen(T, S).",
        )
        edb1 = ruleset.edb()
        edb2 = ruleset.edb()
        barrier = threading.Barrier(2)
        errors = []

        def feed(edb, thread_name, value_prefix):
            try:
                barrier.wait()
                for index in range(16):
                    edb.add_fact("seen", [thread_name, f"{value_prefix}_{index}"])
            except BaseException as exc:  # pragma: no cover - surfaced below
                errors.append(exc)

        threads = [
            threading.Thread(target=feed, args=(edb1, "t1", "a")),
            threading.Thread(target=feed, args=(edb2, "t2", "b")),
        ]
        for thread in threads:
            thread.start()
        for thread in threads:
            thread.join()
        assert errors == []

        result1 = ruleset.solve(edb1)
        result2 = ruleset.solve(edb2)
        assert set(result1.enumerate_predicate_facts("out", 2)) == {
            ("t1", f"a_{index}") for index in range(16)
        }
        assert set(result2.enumerate_predicate_facts("out", 2)) == {
            ("t2", f"b_{index}") for index in range(16)
        }
