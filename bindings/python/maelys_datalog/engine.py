from __future__ import annotations

from collections.abc import Iterable, Sequence
import threading
import weakref

from ._ffi import C, ffi, lib
from ._types import (
    BuildLimits,
    Fact,
    InputTerm,
    Predicate,
    RawFact,
    Term,
    limits_from_c,
)
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


def _validate_arity(arity: int, max_arity: int) -> None:
    if isinstance(arity, bool) or not isinstance(arity, int):
        raise TypeError("arity must be an int, not bool")
    if arity < 0 or arity > max_arity:
        raise ValueError(f"arity {arity} outside supported range 0..{max_arity}")


def _validate_predicate(predicate: str) -> None:
    if not isinstance(predicate, str):
        raise TypeError("predicate must be a str")


def _normalize(predicates: Iterable[Predicate], max_arity: int) -> tuple[Predicate, ...]:
    normalized = []
    for predicate in predicates:
        if not isinstance(predicate, Predicate):
            try:
                predicate = Predicate(predicate.name, predicate.arity, predicate.kind_flags)
            except AttributeError as exc:
                raise TypeError("predicates must contain Predicate-compatible values") from exc
        if not isinstance(predicate.name, str):
            raise TypeError("predicate name must be a str")
        _validate_arity(predicate.arity, max_arity)
        if not predicate.name:
            raise ValueError("predicate name is required")
        normalized.append(
            Predicate(predicate.name, predicate.arity, int(predicate.kind_flags))
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
        normalized = _normalize(predicates, self.limits.max_arity)
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
        self._symbol_lock = threading.RLock()
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
        with self._symbol_lock:
            self._require_open()
            text_b = text.encode("utf-8")
            out = ffi.new("uint32_t *")
            _raise_rc(lib.maelys_py_intern_symbol(self._handle, text_b, out))
            return int(out[0])

    def _lookup_symbol_readonly(self, text: str) -> tuple[bool, int]:
        self._require_open()
        text_b = text.encode("utf-8")
        out = ffi.new("uint32_t *")
        found = ffi.new("int *")
        _raise_rc(
            lib.maelys_py_symbol_lookup_readonly(
                self._handle, text_b, len(text_b), out, found
            )
        )
        return bool(found[0]), int(out[0])

    def _symbol_id_is_valid(self, symbol_id: int) -> bool:
        self._require_open()
        valid = ffi.new("int *")
        _raise_rc(
            lib.maelys_py_symbol_id_is_valid(
                self._handle, int(symbol_id), valid
            )
        )
        return bool(valid[0])

    def symbol_text(self, symbol_id: int) -> str:
        with self._symbol_lock:
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
            if value.kind == C.TERM_SYMBOL:
                self.ruleset.symbol_text(value.value)
            return value
        if isinstance(value, bool):
            return Term.boolean(value)
        if isinstance(value, int):
            return Term.integer(value)
        if isinstance(value, str):
            return Term.symbol_id(self.ruleset.intern_symbol(value))
        raise TypeError(f"unsupported term value {value!r}")

    def add_fact(self, predicate: str, terms: Sequence[InputTerm]) -> None:
        _validate_predicate(predicate)
        if isinstance(terms, (str, bytes)) or not isinstance(terms, Sequence):
            raise TypeError("terms must be a sequence, not str or bytes")
        if len(terms) > self.ruleset.engine.limits.max_arity:
            raise ValueError(
                f"arity {len(terms)} outside supported range "
                f"0..{self.ruleset.engine.limits.max_arity}"
            )
        with self.ruleset._symbol_lock:
            self._require_open()
            if self._closed_for_mutation:
                raise RuntimeError("Edb is closed for mutation after solve()")
            converted = [self._term(value) for value in terms]
            arr = ffi.new("maelys_py_term_t[]", len(converted))
            for i, term in enumerate(converted):
                arr[i].kind = term.kind
                arr[i].value = term.value
            predicate_b = predicate.encode("utf-8")
            _raise_rc(
                lib.maelys_py_edb_add_fact(self._handle, predicate_b, arr, len(converted))
            )


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

    def _validate_query_predicate(self, predicate: str, arity: int) -> None:
        predicate_b = predicate.encode("utf-8")
        _raise_rc(
            lib.maelys_py_result_validate_query_predicate(
                self._handle, predicate_b, arity
            )
        )

    def _contains_fact_resolved(
        self,
        predicate: str,
        terms: Sequence[Term],
    ) -> bool:
        arr = ffi.NULL
        if terms:
            arr = ffi.new("maelys_py_term_t[]", len(terms))
            for i, term in enumerate(terms):
                arr[i].kind = term.kind
                arr[i].value = term.value
        present = ffi.new("int *")
        predicate_b = predicate.encode("utf-8")
        _raise_rc(
            lib.maelys_py_result_contains_fact(
                self._handle,
                predicate_b,
                arr,
                len(terms),
                present,
            )
        )
        return bool(present[0])

    def contains_fact(
        self,
        predicate: str,
        terms: Sequence[InputTerm],
    ) -> bool:
        _validate_predicate(predicate)
        if isinstance(terms, (str, bytes)) or not isinstance(terms, Sequence):
            raise TypeError("terms must be a sequence, not str or bytes")
        arity = len(terms)
        _validate_arity(arity, self.ruleset.engine.limits.max_arity)
        with self.ruleset._symbol_lock:
            self._require_open()
            self.ruleset._require_open()
            self._validate_query_predicate(predicate, arity)
            resolved: list[Term] = []
            missing_symbol = False
            for value in terms:
                if isinstance(value, str):
                    found, symbol_id = self.ruleset._lookup_symbol_readonly(value)
                    if found:
                        resolved.append(Term.symbol_id(symbol_id))
                    else:
                        missing_symbol = True
                    continue
                if isinstance(value, Term):
                    if value.kind == C.TERM_SYMBOL:
                        if (
                            isinstance(value.value, bool)
                            or not isinstance(value.value, int)
                            or value.value <= 0
                            or value.value > 0xFFFFFFFF
                            or not self.ruleset._symbol_id_is_valid(value.value)
                        ):
                            raise MaelysDatalogError(
                                C.ERR_INVALID_FIELD,
                                "unknown symbol id for this Ruleset",
                            )
                    elif value.kind == C.TERM_BOOL:
                        if value.value not in (0, 1):
                            raise MaelysDatalogError(
                                C.ERR_INVALID_FIELD,
                                "boolean term value must be 0 or 1",
                            )
                    elif value.kind == C.TERM_INT:
                        if (
                            isinstance(value.value, bool)
                            or not isinstance(value.value, int)
                            or value.value < -(1 << 63)
                            or value.value > (1 << 63) - 1
                        ):
                            raise MaelysDatalogError(
                                C.ERR_INVALID_FIELD,
                                "integer term value outside int64 range",
                            )
                    else:
                        raise TypeError(f"unsupported term value {value!r}")
                    resolved.append(value)
                    continue
                if isinstance(value, bool):
                    resolved.append(Term.boolean(value))
                    continue
                if isinstance(value, int):
                    resolved.append(Term.integer(value))
                    continue
                raise TypeError(f"unsupported term value {value!r}")
            if missing_symbol:
                return False
            return self._contains_fact_resolved(predicate, resolved)

    def _enumerate_predicate_facts_raw(self, predicate: str, arity: int) -> list[RawFact]:
        self._require_open()
        _validate_predicate(predicate)
        _validate_arity(arity, self.ruleset.engine.limits.max_arity)
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
        facts: list[RawFact] = []
        for i in range(count[0]):
            row: list[Term] = []
            for j in range(arity):
                term = terms[i * arity + j]
                if term.kind == C.TERM_SYMBOL:
                    row.append(Term.symbol_id(term.value))
                elif term.kind == C.TERM_INT:
                    row.append(Term.integer(term.value))
                elif term.kind == C.TERM_BOOL:
                    row.append(Term.boolean(bool(term.value)))
                else:
                    row.append(Term(int(term.kind), int(term.value)))
            facts.append(tuple(row))
        return facts

    def enumerate_predicate_facts_raw(self, predicate: str, arity: int) -> list[RawFact]:
        return self._enumerate_predicate_facts_raw(predicate, arity)

    def enumerate_predicate_facts(self, predicate: str, arity: int) -> list[Fact]:
        raw_facts = self._enumerate_predicate_facts_raw(predicate, arity)
        with self.ruleset._symbol_lock:
            facts: list[Fact] = []
            for raw_fact in raw_facts:
                row = []
                for term in raw_fact:
                    if term.kind == C.TERM_SYMBOL:
                        row.append(self.ruleset.symbol_text(term.value))
                    elif term.kind == C.TERM_INT:
                        row.append(term.value)
                    elif term.kind == C.TERM_BOOL:
                        row.append(bool(term.value))
                    else:
                        raise MaelysDatalogError(
                            C.ERR_INVALID_FIELD,
                            f"unexpected unresolved term kind {term.kind}",
                        )
                facts.append(tuple(row))
            return facts
