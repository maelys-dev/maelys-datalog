"""The Python text convenience owns exactly one native prepared explanation."""

import unittest
from unittest.mock import patch

from maelys_datalog_next import Engine, ExplanationKind, MaelysDatalogError, Predicate, PRED_EDB, PRED_IDB, PRED_QUERY
from maelys_datalog_next import engine as binding


PREFIX = "maelys_datalog_"
SEQUENCE = [
    "result_explanation_storage_requirements",
    "result_prepare_explanation",
    "prepared_explanation_text_size",
    "prepared_explanation_write_text",
    "prepared_explanation_release",
]


class NativeSpy:
    def __init__(self, fail=None):
        self.native = binding.lib
        self.calls = []
        self.fail = fail
        self.fail_once = True
        self.live_storage = None

    def __getattr__(self, name):
        operation = getattr(self.native, name)
        if "explain" not in name and "explanation" not in name:
            return operation
        if name in (PREFIX + "result_explain_true_text", PREFIX + "result_explain_false_text"):
            raise AssertionError("The Python convenience must not use the legacy two-pass path")
        short = name.removeprefix(PREFIX)

        def call(*args):
            self.calls.append(short)
            if short == self.fail and self.fail_once:
                self.fail_once = False
                return self.native.MAELYS_DATALOG_STATUS_PAYLOAD_TOO_LARGE
            if short == "result_prepare_explanation":
                # Exercise the real native lease and the actual aligned workspace.
                required, alignment = binding.ffi.new("size_t *"), binding.ffi.new("size_t *")
                self.native.maelys_datalog_result_explanation_storage_requirements(
                    args[0], args[1], required, alignment)
                assert int(binding.ffi.cast("uintptr_t", args[5])) % alignment[0] == 0
                assert args[6] == required[0]
                self.live_storage = args[5]
            return operation(*args)
        return call


class PreparedExplanationTest(unittest.TestCase):
    def setUp(self):
        self.engine = Engine()
        self.addCleanup(self.engine.close)
        self.engine.register_domain("next_prepared", [
            Predicate("seed", 1, PRED_EDB), Predicate("blocked", 1, PRED_EDB),
            Predicate("allow", 1, PRED_IDB | PRED_QUERY),
        ])
        self.rules = self.engine.load_inline_ruleset(
            "next_prepared", "prepared", "allow(X) :- seed(X), not(blocked(X)).")
        self.edb = self.rules.edb()
        self.edb.add_facts([("seed", ["alice"]), ("seed", ["mallory"]),
                            ("blocked", ["mallory"])])
        self.session = self.rules.prepare()

    def test_session_workspace_uses_cached_text_without_a_per_call_arena(self):
        with self.session.solve(self.edb) as result:
            expected = (result.explain_true("allow", ["alice"]), result.explain_false("allow", ["mallory"]))
        native, real_ffi = binding.lib, binding.ffi
        calls, allocations = [], []

        class DirectSpy:
            def __getattr__(self, name):
                operation = getattr(native, name)
                if "explain" not in name and "explanation" not in name:
                    return operation
                if name not in (PREFIX + "result_explain_true_text", PREFIX + "result_explain_false_text"):
                    raise AssertionError("Configured Python should reuse the session workspace")
                def call(*args):
                    calls.append(name)
                    return operation(*args)
                return call

        class FfiSpy:
            def __getattr__(self, name):
                return getattr(real_ffi, name)
            def new(self, declaration, *args):
                allocations.append(declaration)
                return real_ffi.new(declaration, *args)

        with self.rules.prepare(explanations=ExplanationKind.TRUE | ExplanationKind.FALSE) as session:
            self.assertEqual(session.execution_fingerprint, self.session.execution_fingerprint)
            for _ in range(2):
                with session.solve(self.edb) as result:
                    with patch.object(binding, "lib", DirectSpy()), patch.object(binding, "ffi", FfiSpy()):
                        for i in range(40):
                            text = (result.explain_false("allow", ["mallory"]) if i % 2
                                    else result.explain_true("allow", ["alice"]))
                            self.assertEqual(text, expected[i % 2])
        self.assertEqual(len(calls), 160)  # two calls per explanation; C counts preparation
        self.assertNotIn("unsigned char[]", allocations)
        self.assertIn("char[]", allocations)  # output/Python allocations are not hidden

    def test_workspace_option_validation_and_unreserved_kind(self):
        for invalid, error in ((True, TypeError), ("true", TypeError), (-1, ValueError), (4, ValueError)):
            with self.subTest(value=invalid), self.assertRaises(error):
                self.rules.prepare(explanations=invalid)
        with self.rules.prepare(explanations=ExplanationKind.TRUE) as session:
            with session.solve(self.edb) as result:
                self.assertIn("document=why-true", result.explain_true("allow", ["alice"]))
                with self.assertRaises(MaelysDatalogError):
                    result.explain_false("allow", ["mallory"])
        # The one-shot Ruleset convenience forwards the opt-in, without changing defaults.
        with self.rules.solve(self.edb, explanations=ExplanationKind.FALSE) as result:
            self.assertIn("document=why-false", result.explain_false("allow", ["mallory"]))

    def test_cached_output_failures_do_not_leak_a_result_lease(self):
        native, real_ffi = binding.lib, binding.ffi
        class FailingOutput:
            def __getattr__(self, name):
                return getattr(real_ffi, name)
            def new(self, declaration, *args):
                if declaration == "char[]" and args and isinstance(args[0], int):
                    raise MemoryError("injected cached output failure")
                return real_ffi.new(declaration, *args)
        class FailingWrite:
            def __getattr__(self, name):
                operation = getattr(native, name)
                if name != PREFIX + "result_explain_true_text":
                    return operation
                def call(*args):
                    if args[4] != real_ffi.NULL:
                        return -7  # IO after a successful native measure/cache fill
                    return operation(*args)
                return call
        class InvalidUTF8:
            def __getattr__(self, name):
                operation = getattr(native, name)
                if name != PREFIX + "result_explain_true_text":
                    return operation
                def call(*args):
                    rc = operation(*args)
                    if rc == 0 and args[4] != real_ffi.NULL:
                        args[4][0] = b"\xff"
                    return rc
                return call
        with self.rules.prepare(explanations=ExplanationKind.TRUE) as session:
            for target, replacement, error in (("ffi", FailingOutput(), MemoryError),
                                                ("lib", FailingWrite(), MaelysDatalogError),
                                                ("lib", InvalidUTF8(), UnicodeDecodeError)):
                with session.solve(self.edb) as result:
                    with patch.object(binding, target, replacement), self.assertRaises(error):
                        result.explain_true("allow", ["alice"])
                # Context close must release the cache even if Python never rendered it.
            with session.solve(self.edb) as result:
                self.assertIn("document=why-true", result.explain_true("allow", ["alice"]))

    def test_each_kind_prepares_once_and_returns_independent_text(self):
        result = self.session.solve(self.edb)
        outputs = []
        for method, person, header in [
            (result.explain_true, "alice", "MAELYS-DATALOG-v2\ndocument=why-true"),
            (result.explain_false, "mallory", "MAELYS-DATALOG-v2\ndocument=why-false"),
        ]:
            spy = NativeSpy()
            with patch.object(binding, "lib", spy):
                text = method("allow", [person])
            self.assertEqual(spy.calls, SEQUENCE)
            self.assertTrue(text.startswith(header), text)
            outputs.append(text)
        result.close()  # Would fail if either prepared handle retained its lease.
        with self.session.solve(self.edb) as again:
            self.assertEqual(again.explain_true("allow", ["alice"]), outputs[0])
        self.assertIn("status=complete", outputs[1])  # Still valid after closing results.

    def test_native_failure_releases_only_a_successfully_prepared_handle(self):
        for stage in SEQUENCE[:-1]:
            with self.subTest(stage=stage):
                result = self.session.solve(self.edb)
                spy = NativeSpy(fail=stage)
                with patch.object(binding, "lib", spy):
                    with self.assertRaises(MaelysDatalogError):
                        result.explain_false("allow", ["mallory"])
                expected = SEQUENCE[:SEQUENCE.index(stage) + 1]
                if SEQUENCE.index(stage) >= 2:
                    expected = expected + ["prepared_explanation_release"]
                self.assertEqual(spy.calls, expected)
                result.close()
                with self.session.solve(self.edb) as again:
                    self.assertTrue(again.contains_fact("allow", ["alice"]))

    def test_python_output_allocation_failure_releases_the_result_lease(self):
        result = self.session.solve(self.edb)
        real_ffi = binding.ffi

        class FailingOutput:
            def __getattr__(self, name):
                return getattr(real_ffi, name)

            def new(self, declaration, *args):
                if declaration == "char[]" and args and isinstance(args[0], int):
                    raise MemoryError("injected output allocation failure")
                return real_ffi.new(declaration, *args)

        spy = NativeSpy()
        with patch.object(binding, "lib", spy), patch.object(binding, "ffi", FailingOutput()):
            with self.assertRaisesRegex(MemoryError, "output allocation"):
                result.explain_true("allow", ["alice"])
        self.assertEqual(spy.calls, SEQUENCE[:3] + [SEQUENCE[-1]])
        result.close()

    def test_unknown_symbol_failure_and_closed_result(self):
        result = self.session.solve(self.edb)
        spy = NativeSpy()
        with patch.object(binding, "lib", spy):
            with self.assertRaises(MaelysDatalogError):
                result.explain_false("allow", ["never-interned"])
        self.assertEqual(spy.calls, SEQUENCE[:2])
        result.close()
        with self.assertRaisesRegex(RuntimeError, "closed"):
            result.explain_true("allow", ["alice"])

    def test_utf8_decode_failure_releases_the_result_lease(self):
        result = self.session.solve(self.edb)

        class InvalidUTF8(NativeSpy):
            def __getattr__(self, name):
                operation = super().__getattr__(name)
                if name != PREFIX + "prepared_explanation_write_text":
                    return operation

                def write(*args):
                    rc = operation(*args)
                    if rc == self.native.MAELYS_DATALOG_STATUS_OK:
                        args[1][0] = b"\xff"
                    return rc
                return write

        spy = InvalidUTF8()
        with patch.object(binding, "lib", spy):
            with self.assertRaises(UnicodeDecodeError):
                result.explain_true("allow", ["alice"])
        self.assertEqual(spy.calls, SEQUENCE)
        result.close()
