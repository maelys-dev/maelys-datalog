from __future__ import annotations

import threading
from typing import Iterable, Sequence
import weakref

from ._ffi import C, ffi, lib
from ._types import BuildLimits, Predicate, Term, limits_from_c
from .errors import (
    DomainAlreadyRegisteredError,
    DomainRegistryFullError,
    MaelysDatalogError,
)


_DOMAIN_REGISTRY_LOCK = threading.Lock()


def _raise_rc(rc: int, message: str = "", hint: str = "") -> None:
    if rc == C.OK:
        return
    if rc == C.ERR_PAYLOAD_TOO_LARGE:
        raise DomainRegistryFullError(rc, message or "process-wide domain registry full")
    raise MaelysDatalogError(rc, message, hint)


def _normalize(predicates: Iterable[Predicate]) -> tuple[Predicate, ...]:
    normalized = []
    for predicate in predicates:
        if not isinstance(predicate, Predicate):
            predicate = Predicate(predicate.name, predicate.arity, predicate.kind_flags)
        if not predicate.name or predicate.arity < 0:
            raise ValueError("predicate name and arity are required")
        normalized.append(
            Predicate(str(predicate.name), int(predicate.arity), int(predicate.kind_flags))
        )
    return tuple(normalized)


class Engine:
    def __init__(self):
        self._handle = lib.maelys_py_engine_new()
        if self._handle == ffi.NULL:
            raise MemoryError("failed to allocate Maelys engine")
        limits = ffi.new("maelys_datalog_build_limits_t *")
        _raise_rc(lib.maelys_py_get_build_limits(limits))
        self.limits: BuildLimits = limits_from_c(limits)
        self._closed = False
        self._rulesets = weakref.WeakSet()

    def close(self) -> None:
        if not self._closed:
            for ruleset in list(self._rulesets):
                ruleset.close()
            lib.maelys_py_engine_free(self._handle)
            self._handle = ffi.NULL
            self._closed = True

    def __enter__(self) -> "Engine":
        return self

    def __exit__(self, *_exc) -> None:
        self.close()

    def __del__(self):
        try:
            self.close()
        except Exception:
            pass

    def _require_open(self) -> None:
        if self._closed:
            raise RuntimeError("Engine is closed")

    def _find_domain(self, domain_name: str):
        domain_b = domain_name.encode("utf-8")
        count = ffi.new("size_t *")
        found = ffi.new("int *")
        inspectable = ffi.new("int *")
        rc = lib.maelys_py_find_domain(domain_b, ffi.NULL, 0, count, found, inspectable)
        _raise_rc(rc)
        if not found[0] or not inspectable[0]:
            return bool(found[0]), bool(inspectable[0]), ()
        preds = ffi.new("maelys_py_predicate_def_t[]", count[0])
        rc = lib.maelys_py_find_domain(domain_b, preds, count[0], count, found, inspectable)
        _raise_rc(rc)
        copied = []
        for i in range(count[0]):
            copied.append(
                Predicate(
                    ffi.string(preds[i].name).decode("utf-8"),
                    int(preds[i].arity),
                    int(preds[i].kind_flags),
                )
            )
        return True, True, tuple(copied)

    def register_domain(self, domain_name: str, predicates: Iterable[Predicate]) -> None:
        self._require_open()
        normalized = _normalize(predicates)
        with _DOMAIN_REGISTRY_LOCK:
            found, inspectable, existing = self._find_domain(domain_name)
            if found and not inspectable:
                raise DomainAlreadyRegisteredError(
                    C.ERR_UNSUPPORTED,
                    f"domain {domain_name!r} exists via native callback and cannot be inspected",
                )
            if found and existing == normalized:
                return
            if found:
                raise DomainAlreadyRegisteredError(
                    C.ERR_INVALID_FIELD,
                    f"domain {domain_name!r} already exists with a different predicate table",
                )

            domain_b = domain_name.encode("utf-8")
            name_buffers = [ffi.new("char[]", pred.name.encode("utf-8")) for pred in normalized]
            pred_array = ffi.new("maelys_py_predicate_def_t[]", len(normalized))
            for i, pred in enumerate(normalized):
                pred_array[i].name = name_buffers[i]
                pred_array[i].arity = pred.arity
                pred_array[i].kind_flags = pred.kind_flags
            rc = lib.maelys_py_register_domain(domain_b, pred_array, len(normalized))
            if rc == C.ERR_PAYLOAD_TOO_LARGE:
                raise DomainRegistryFullError(
                    rc,
                    "process-wide domain registry full (16 max, no eviction API); "
                    "reuse stable domain_name values across hot reloads",
                )
            _raise_rc(rc)

    def load_inline_ruleset(self, domain_name: str, ruleset_id: str, source: str) -> "Ruleset":
        self._require_open()
        with _DOMAIN_REGISTRY_LOCK:
            domain_b = domain_name.encode("utf-8")
            ruleset_b = ruleset_id.encode("utf-8")
            source_b = source.encode("utf-8")
            out = ffi.new("maelys_py_ruleset_t **")
            rc = lib.maelys_py_load_inline_ruleset(
                self._handle, domain_b, ruleset_b, source_b, len(source_b), out
            )
            if rc != C.OK:
                message = ffi.string(lib.maelys_py_last_diag_message(self._handle)).decode("utf-8")
                hint = ffi.string(lib.maelys_py_last_diag_hint(self._handle)).decode("utf-8")
                raise MaelysDatalogError(rc, message, hint)
            ruleset = Ruleset(out[0], self)
            self._rulesets.add(ruleset)
            return ruleset


class Ruleset:
    def __init__(self, handle, engine: Engine):
        self._handle = handle
        self.engine = engine
        self._closed = False
        self._edbs = weakref.WeakSet()
        self._results = weakref.WeakSet()

    def close(self) -> None:
        if not self._closed:
            for result in list(self._results):
                result.close()
            for edb in list(self._edbs):
                edb.close()
            lib.maelys_py_ruleset_free(self._handle)
            self._handle = ffi.NULL
            self._closed = True

    def __del__(self):
        try:
            self.close()
        except Exception:
            pass

    def _require_open(self) -> None:
        if self._closed:
            raise RuntimeError("Ruleset is closed")

    def intern_symbol(self, text: str) -> int:
        self._require_open()
        text_b = text.encode("utf-8")
        out = ffi.new("uint32_t *")
        _raise_rc(lib.maelys_py_intern_symbol(self._handle, text_b, out))
        return int(out[0])

    def symbol_text(self, symbol_id: int) -> str:
        self._require_open()
        text = lib.maelys_py_symbol_text(self._handle, int(symbol_id))
        if text == ffi.NULL:
            raise MaelysDatalogError(C.ERR_INVALID_FIELD, "unknown symbol id")
        return ffi.string(text).decode("utf-8")

    def edb(self) -> "Edb":
        self._require_open()
        handle = lib.maelys_py_edb_new(self._handle)
        if handle == ffi.NULL:
            raise MemoryError("failed to allocate EDB")
        edb = Edb(handle, self)
        self._edbs.add(edb)
        return edb

    def solve(self, edb: "Edb") -> "SolveResult":
        self._require_open()
        if edb.ruleset is not self:
            raise RuntimeError("Edb belongs to a different Ruleset")
        edb._require_open()
        out = ffi.new("maelys_py_result_t **")
        rc = lib.maelys_py_solve(self._handle, edb._handle, out)
        if rc != C.OK:
            raise MaelysDatalogError(rc)
        edb._closed_for_mutation = True
        result = SolveResult(out[0], self)
        self._results.add(result)
        return result


class Edb:
    def __init__(self, handle, ruleset: Ruleset):
        self._handle = handle
        self.ruleset = ruleset
        self._closed = False
        self._closed_for_mutation = False

    def close(self) -> None:
        if not self._closed:
            lib.maelys_py_edb_free(self._handle)
            self._handle = ffi.NULL
            self._closed = True

    def __del__(self):
        try:
            self.close()
        except Exception:
            pass

    def _require_open(self) -> None:
        if self._closed:
            raise RuntimeError("Edb is closed")

    def _term(self, value) -> Term:
        if isinstance(value, Term):
            return value
        if isinstance(value, bool):
            return Term.boolean(value)
        if isinstance(value, int):
            return Term.integer(value)
        if isinstance(value, str):
            return Term.symbol_id(self.ruleset.intern_symbol(value))
        raise TypeError(f"unsupported term value {value!r}")

    def add_fact(self, predicate: str, terms: Sequence[object]) -> None:
        self._require_open()
        if self._closed_for_mutation:
            raise RuntimeError("Edb is closed for mutation after solve()")
        converted = [self._term(value) for value in terms]
        arr = ffi.new("maelys_py_term_t[]", len(converted))
        for i, term in enumerate(converted):
            arr[i].kind = term.kind
            arr[i].value = term.value
        predicate_b = predicate.encode("utf-8")
        _raise_rc(lib.maelys_py_edb_add_fact(self._handle, predicate_b, arr, len(converted)))


class SolveResult:
    def __init__(self, handle, ruleset: Ruleset):
        self._handle = handle
        self.ruleset = ruleset
        self._closed = False

    def close(self) -> None:
        if not self._closed:
            lib.maelys_py_result_free(self._handle)
            self._handle = ffi.NULL
            self._closed = True

    def __del__(self):
        try:
            self.close()
        except Exception:
            pass

    def _require_open(self) -> None:
        if self._closed:
            raise RuntimeError("SolveResult is closed")

    def derived_fact_count(self) -> int:
        self._require_open()
        out = ffi.new("size_t *")
        _raise_rc(lib.maelys_py_result_derived_fact_count(self._handle, out))
        return int(out[0])

    def enumerate_predicate_facts(self, predicate: str, arity: int) -> list[tuple[object, ...]]:
        self._require_open()
        predicate_b = predicate.encode("utf-8")
        count = ffi.new("size_t *")
        _raise_rc(
            lib.maelys_py_result_enumerate_predicate_facts(
                self._handle, predicate_b, arity, ffi.NULL, 0, count
            )
        )
        if count[0] == 0:
            return []
        terms = ffi.new("maelys_py_term_t[]", count[0] * arity)
        _raise_rc(
            lib.maelys_py_result_enumerate_predicate_facts(
                self._handle, predicate_b, arity, terms, count[0], count
            )
        )
        facts = []
        for i in range(count[0]):
            row = []
            for j in range(arity):
                term = terms[i * arity + j]
                if term.kind == C.TERM_SYMBOL:
                    row.append(self.ruleset.symbol_text(term.value))
                elif term.kind == C.TERM_INT:
                    row.append(int(term.value))
                elif term.kind == C.TERM_BOOL:
                    row.append(bool(term.value))
                else:
                    row.append(Term(int(term.kind), int(term.value)))
            facts.append(tuple(row))
        return facts
