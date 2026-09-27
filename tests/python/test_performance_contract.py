# SPDX-License-Identifier: MPL-2.0
"""Deterministic lifecycle budgets; timing belongs to the separate workflow."""
from collections import Counter
from contextlib import closing
from unittest.mock import patch

from maelys_datalog import Engine, Predicate, PRED_EDB, PRED_IDB, PRED_QUERY
from maelys_datalog import engine as binding


def test_default_solve_lifecycle_does_not_grow_with_input():
    native = binding.lib
    calls = Counter()

    class Spy:
        def __getattr__(self, name):
            operation = getattr(native, name)
            if not name.startswith("maelys_datalog_") or not callable(operation):
                return operation
            def call(*args):
                calls[name.removeprefix("maelys_datalog_")] += 1
                return operation(*args)
            return call

    with Engine() as engine:
        engine.register_domain("python_perf_contract", [Predicate("seed", 1, PRED_EDB),
            Predicate("extra1", 1, PRED_EDB), Predicate("extra2", 1, PRED_EDB),
            Predicate("allow", 1, PRED_IDB | PRED_QUERY)])
        with engine.load_inline_ruleset("python_perf_contract", "perf", "allow(X) :- seed(X).") as rules:
            for size in (1, 93):
                with closing(rules.edb()) as edb:
                    edb.add_facts([(("seed", "extra1", "extra2")[i // 31], [f"user{i}"])
                                   for i in range(size)])
                    for prepared in (False, True):
                        calls.clear()
                        with patch.object(binding, "lib", Spy()):
                            session = rules.prepare() if prepared else None
                            for _ in range(3):
                                with (session.solve(edb) if session else rules.solve(edb)) as result:
                                    assert result.contains_fact("allow", ["user0"])
                                    assert not result.contains_fact("allow", ["absent"])
                            if session:
                                session.close()
                        # Covers ownership/release as well as construction; preparation
                        # and result materialization must not be repeated per fact.
                        assert calls["session_create"] == (1 if prepared else 3)
                        assert calls["session_free"] == calls["session_create"]
                        assert calls["session_solve_edb"] == 3
                        assert calls["result_free"] == 3
                        assert calls["result_query"] == 6
                        assert calls["diagnostic_init"] <= 3
                        assert sum(calls.values()) <= (18 if prepared else 24)
                        assert not any("config" in name or "explanation" in name or
                                       name.startswith("policy_load") for name in calls)
