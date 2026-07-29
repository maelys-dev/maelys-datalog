from __future__ import annotations

import os
import subprocess
import sys
import textwrap
import threading

import pytest

import maelys_datalog as md


def test_ground_query_cffi_exports_are_present():
    for name in (
        "maelys_py_symbol_lookup_readonly",
        "maelys_py_symbol_id_is_valid",
        "maelys_py_result_validate_query_predicate",
        "maelys_py_result_contains_fact",
        "maelys_py_result_explain_fact_text",
    ):
        assert hasattr(md._ffi.lib, name)


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
        expected = (
            "MAELYS-DATALOG-WHY-TRUE-TEXT-v1\n"
            "status=complete\n"
            "steps=1 premises=1\n"
            'step=0 rule=1 fact="path"("a","b")\n'
            'premise=0 body=0 kind=positive origin=edb '
            'fact="edge"("a","b") parent=-\n'
            "result-step=0\n"
        )
        assert result.explain_fact_text("path", ["a", "b"]) == expected
        assert result.explain_fact_text("path", ["a", "b"]) == expected
        assert result.explain_fact_text("path", ["c", "a"]) is None
        result.close()
        edb.close()
        ruleset.close()


def test_contains_fact_covers_policy_edb_and_idb_without_interning(monkeypatch):
    query_predicates = [
        md.Predicate(
            "policy_enabled",
            1,
            md.PRED_POLICY_FACT | md.PRED_QUERY,
        ),
        md.Predicate("observed", 1, md.PRED_EDB | md.PRED_QUERY),
        md.Predicate("derived", 1, md.PRED_IDB | md.PRED_QUERY),
        md.Predicate("access", 2, md.PRED_EDB),
        md.Predicate("allow", 2, md.PRED_IDB | md.PRED_QUERY),
    ]
    with md.Engine() as engine:
        engine.register_domain("py_test_contains_fact", query_predicates)
        ruleset = engine.load_inline_ruleset(
            "py_test_contains_fact",
            "policy",
            "policy_enabled(true).\n"
            "derived(X) :- observed(X).\n"
            "allow(U, D) :- access(U, D).",
        )
        edb = ruleset.edb()
        edb.add_fact("observed", ["alice"])
        edb.add_fact("access", ["alice", "roadmap.pdf"])
        edb.add_fact("access", ['café\n"', "δ"])
        result = ruleset.solve(edb)

        assert result.contains_fact("policy_enabled", [True])
        assert not result.contains_fact("policy_enabled", [1])
        assert result.contains_fact("observed", ["alice"])
        assert result.contains_fact("derived", ["alice"])
        assert result.contains_fact("allow", ["alice", "roadmap.pdf"])
        assert result.enumerate_predicate_facts("policy_enabled", 1) == []
        assert result.enumerate_predicate_facts("observed", 1) == []
        assert result.enumerate_predicate_facts("derived", 1) == [("alice",)]
        assert result.explain_fact_text("policy_enabled", [True]) is None
        assert result.explain_fact_text("observed", ["alice"]) is None
        derived_text = result.explain_fact_text("derived", ["alice"])
        assert derived_text is not None
        assert derived_text.startswith("MAELYS-DATALOG-WHY-TRUE-TEXT-v1\n")
        assert "status=complete\n" in derived_text
        escaped_text = result.explain_fact_text("allow", ['café\n"', "δ"])
        assert escaped_text is not None
        assert '"café\\n\\""' in escaped_text
        assert '"δ"' in escaped_text

        found_before, id_before = ruleset._lookup_symbol_readonly("unknown")
        assert not found_before
        assert id_before == 0

        def fail_intern(_text):
            raise AssertionError("contains_fact must never intern symbols")

        monkeypatch.setattr(ruleset, "intern_symbol", fail_intern)
        assert not result.contains_fact("allow", ["unknown", "roadmap.pdf"])
        assert result.explain_fact_text("allow", ["unknown", "roadmap.pdf"]) is None
        found_after, id_after = ruleset._lookup_symbol_readonly("unknown")
        assert not found_after
        assert id_after == 0

        with pytest.raises(md.MaelysDatalogError) as exc:
            result.contains_fact("missing", ["unknown"])
        assert exc.value.code == md.C.ERR_INVALID_FIELD
        with pytest.raises(md.MaelysDatalogError) as exc:
            result.explain_fact_text("missing", ["unknown"])
        assert exc.value.code == md.C.ERR_INVALID_FIELD

def test_contains_fact_validates_python_inputs_and_explicit_terms():
    with md.Engine() as engine:
        ruleset = make_ruleset(engine, "py_test_contains_validation")
        edb = ruleset.edb()
        edb.add_fact("edge", ["a", "b"])
        result = ruleset.solve(edb)

        for invalid_terms in ("a", b"a"):
            with pytest.raises(TypeError, match="sequence"):
                result.contains_fact("path", invalid_terms)
            with pytest.raises(TypeError, match="sequence"):
                result.explain_fact_text("path", invalid_terms)
        with pytest.raises(TypeError, match="predicate"):
            result.contains_fact(123, ["a", "b"])
        with pytest.raises(TypeError, match="predicate"):
            result.explain_fact_text(123, ["a", "b"])
        with pytest.raises(ValueError, match="outside supported range"):
            result.contains_fact(
                "path",
                [0] * (engine.limits.max_arity + 1),
            )
        with pytest.raises(ValueError, match="outside supported range"):
            result.explain_fact_text(
                "path",
                [0] * (engine.limits.max_arity + 1),
            )
        with pytest.raises(TypeError, match="unsupported term"):
            result.contains_fact("path", [object(), "b"])
        with pytest.raises(TypeError, match="unsupported term"):
            result.explain_fact_text("path", [object(), "b"])
        with pytest.raises(TypeError, match="unsupported term"):
            result.contains_fact("path", [md.Term(md.TERM_VAR, 0), "b"])

        for invalid_id in (0, -1, 1 << 40):
            with pytest.raises(md.MaelysDatalogError) as exc:
                result.contains_fact(
                    "path",
                    [md.Term.symbol_id(invalid_id), "b"],
                )
            assert exc.value.code == md.C.ERR_INVALID_FIELD
            with pytest.raises(md.MaelysDatalogError) as exc:
                result.explain_fact_text(
                    "path",
                    [md.Term.symbol_id(invalid_id), "b"],
                )
            assert exc.value.code == md.C.ERR_INVALID_FIELD

        with pytest.raises(md.MaelysDatalogError) as exc:
            result.contains_fact(
                "path",
                ["unknown", md.Term.symbol_id(999999)],
            )
        assert exc.value.code == md.C.ERR_INVALID_FIELD

        assert not result.contains_fact(
            "path",
            [md.Term.boolean(True), "b"],
        )
        assert result.explain_fact_text(
            "path",
            [md.Term.boolean(True), "b"],
        ) is None
        with pytest.raises(md.MaelysDatalogError) as exc:
            result.contains_fact(
                "path",
                [md.Term(md.TERM_BOOL, 2), "b"],
            )
        assert exc.value.code == md.C.ERR_INVALID_FIELD
        with pytest.raises(md.MaelysDatalogError) as exc:
            result.explain_fact_text(
                "path",
                [md.Term(md.TERM_BOOL, 2), "b"],
            )
        assert exc.value.code == md.C.ERR_INVALID_FIELD


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
    with pytest.raises(RuntimeError):
        result.explain_fact_text("path", ["a", "b"])


def test_explain_fact_text_or_matches_manual_expansion():
    predicates = [
        md.Predicate("edge", 2, md.PRED_EDB),
        md.Predicate("link", 2, md.PRED_EDB),
        md.Predicate("path", 2, md.PRED_IDB | md.PRED_QUERY),
    ]
    with md.Engine() as engine:
        engine.register_domain("py_test_explain_or", predicates)
        ruleset_or = engine.load_inline_ruleset(
            "py_test_explain_or",
            "policy-or",
            "path(X, Y) :- edge(X, Y) or link(X, Y).",
        )
        ruleset_manual = engine.load_inline_ruleset(
            "py_test_explain_or",
            "policy-manual",
            "path(X, Y) :- edge(X, Y).\n"
            "path(X, Y) :- link(X, Y).",
        )
        edb_or = ruleset_or.edb()
        edb_manual = ruleset_manual.edb()
        edb_or.add_fact("link", ["alice", "doc"])
        edb_manual.add_fact("link", ["alice", "doc"])
        result_or = ruleset_or.solve(edb_or)
        result_manual = ruleset_manual.solve(edb_manual)
        assert result_or.explain_fact_text(
            "path", ["alice", "doc"]
        ) == result_manual.explain_fact_text("path", ["alice", "doc"])


def test_explain_fact_text_distinguishes_truncated_from_absent():
    predicates = [
        md.Predicate("edge", 2, md.PRED_EDB),
        md.Predicate("path", 2, md.PRED_IDB | md.PRED_QUERY),
        md.Predicate("reach", 2, md.PRED_IDB | md.PRED_QUERY),
    ]
    with md.Engine() as engine:
        engine.register_domain("py_test_explain_truncated", predicates)
        ruleset = engine.load_inline_ruleset(
            "py_test_explain_truncated",
            "policy",
            "path(X, Y) :- edge(X, Y).\n"
            "path(X, Z) :- path(X, Y), edge(Y, Z).\n"
            "reach(X, Y) :- path(X, Y).",
        )
        edb = ruleset.edb()
        for i in range(8):
            edb.add_fact("edge", [f"n{i}", f"n{i + 1}"])
        result = ruleset.solve(edb)

        assert result.contains_fact("path", ["n0", "n8"])
        text = result.explain_fact_text("path", ["n0", "n8"])
        assert text == (
            "MAELYS-DATALOG-WHY-TRUE-TEXT-v1\n"
            "status=truncated\n"
            "steps=0 premises=0\n"
        )
        assert text is not None
        assert result.explain_fact_text("path", ["n8", "n0"]) is None


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
