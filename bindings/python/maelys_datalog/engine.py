"""Python surface over the opaque Maelys Datalog C facade."""

from __future__ import annotations

from collections.abc import Iterable, Sequence
from dataclasses import dataclass, field
import os
from enum import IntEnum, IntFlag
import threading
import warnings

from ._maelys_cffi import ffi, lib


PRED_EDB = int(lib.MAELYS_DATALOG_PREDICATE_EDB)
PRED_IDB = int(lib.MAELYS_DATALOG_PREDICATE_IDB)
PRED_QUERY = int(lib.MAELYS_DATALOG_PREDICATE_QUERY)
PRED_POLICY_FACT = int(lib.MAELYS_DATALOG_PREDICATE_POLICY_FACT)
_REGISTRY_LOCK = threading.RLock()
_INPUTS_TOKEN = object()
_INT64_MIN = -(1 << 63)
_INT64_MAX = (1 << 63) - 1


class Status(IntEnum):
    """Native operation statuses; diagnostic.code names the detailed cause."""

    OK = int(lib.MAELYS_DATALOG_STATUS_OK)
    INVALID_ARGUMENT = int(lib.MAELYS_DATALOG_STATUS_INVALID_ARGUMENT)
    INVALID_FIELD = int(lib.MAELYS_DATALOG_STATUS_INVALID_FIELD)
    NOT_FOUND = int(lib.MAELYS_DATALOG_STATUS_NOT_FOUND)
    NOT_IMPLEMENTED = int(lib.MAELYS_DATALOG_STATUS_NOT_IMPLEMENTED)
    UNSUPPORTED = int(lib.MAELYS_DATALOG_STATUS_UNSUPPORTED)
    TIMEOUT = int(lib.MAELYS_DATALOG_STATUS_TIMEOUT)
    IO = int(lib.MAELYS_DATALOG_STATUS_IO)
    INTERNAL = int(lib.MAELYS_DATALOG_STATUS_INTERNAL)
    UNAUTHORIZED = int(lib.MAELYS_DATALOG_STATUS_UNAUTHORIZED)
    FORBIDDEN = int(lib.MAELYS_DATALOG_STATUS_FORBIDDEN)
    RATE_LIMITED = int(lib.MAELYS_DATALOG_STATUS_RATE_LIMITED)
    PAYLOAD_TOO_LARGE = int(lib.MAELYS_DATALOG_STATUS_PAYLOAD_TOO_LARGE)
    INVALID_STATE = int(lib.MAELYS_DATALOG_STATUS_INVALID_STATE)
    STORAGE_TOO_SMALL = int(lib.MAELYS_DATALOG_STATUS_STORAGE_TOO_SMALL)


class Capability(IntFlag):
    """Required backend capabilities; unsupported requests fail, never fall back."""

    POSITIVE = int(lib.MAELYS_DATALOG_CAP_POSITIVE)
    NEGATION = int(lib.MAELYS_DATALOG_CAP_NEGATION)
    COMPARISONS = int(lib.MAELYS_DATALOG_CAP_COMPARISONS)
    ARITHMETIC = int(lib.MAELYS_DATALOG_CAP_ARITHMETIC)
    FILTERS = int(lib.MAELYS_DATALOG_CAP_FILTERS)
    EXPLAIN_TRUE = int(lib.MAELYS_DATALOG_CAP_EXPLAIN_TRUE)
    WORK_LIMIT = int(lib.MAELYS_DATALOG_CAP_WORK_LIMIT)
    EXPLAIN_FALSE = int(lib.MAELYS_DATALOG_CAP_EXPLAIN_FALSE)
    AGGREGATES = int(lib.MAELYS_DATALOG_CAP_AGGREGATES)
    MIN = int(lib.MAELYS_DATALOG_CAP_MIN)
    MAX = int(lib.MAELYS_DATALOG_CAP_MAX)
    SUM = int(lib.MAELYS_DATALOG_CAP_SUM)


class ExplanationKind(IntFlag):
    """Opt-in reusable workspace kinds, distinct from backend capability bits."""

    TRUE = int(lib.MAELYS_DATALOG_EXPLAIN_TRUE)
    FALSE = int(lib.MAELYS_DATALOG_EXPLAIN_FALSE)


@dataclass(frozen=True)
class Predicate:
    """Domain declaration; constructors do not register or validate a domain."""

    name: str
    arity: int
    flags: int

    @classmethod
    def edb(cls, name: str, arity: int) -> Predicate:
        """Declare application-supplied facts (EDB)."""
        return cls(name, arity, PRED_EDB)

    @classmethod
    def edb_query(cls, name: str, arity: int) -> Predicate:
        """Declare application-supplied facts that callers may query."""
        return cls(name, arity, PRED_EDB | PRED_QUERY)

    @classmethod
    def idb(cls, name: str, arity: int) -> Predicate:
        """Declare derived facts (IDB), without exposing a query surface."""
        return cls(name, arity, PRED_IDB)

    @classmethod
    def idb_query(cls, name: str, arity: int) -> Predicate:
        """Declare derived facts that callers may query (IDB | QUERY)."""
        return cls(name, arity, PRED_IDB | PRED_QUERY)

    @classmethod
    def policy_fact(cls, name: str, arity: int) -> Predicate:
        """Declare facts supplied by trusted policy source, not request inputs."""
        return cls(name, arity, PRED_POLICY_FACT)

    @classmethod
    def policy_fact_query(cls, name: str, arity: int) -> Predicate:
        """Declare policy-source facts that callers may query."""
        return cls(name, arity, PRED_POLICY_FACT | PRED_QUERY)


@dataclass(frozen=True)
class Limits:
    """Read-only capacities reported by the loaded native library."""

    max_symbols: int
    string_pool_bytes: int
    max_predicates: int
    max_rules: int
    max_arity: int
    max_body_literals: int
    max_depth: int
    max_edb_facts: int
    max_idb_facts: int
    max_facts_per_pred: int
    max_string_bytes: int
    input_edb_text_bytes: int
    max_policy_atoms: int
    max_policy_atom_bytes: int

    @classmethod
    def _read(cls) -> Limits:
        values = {}
        out = ffi.new("size_t *")
        for name in cls.__dataclass_fields__:
            key = getattr(lib, "MAELYS_DATALOG_LIMIT_" + name.upper())
            _check(lib.maelys_datalog_limit_get(key, out), "read build limit")
            values[name] = int(out[0])
        return cls(**values)


@dataclass(frozen=True)
class ProgramCounts:
    """Immutable compiled counts, distinct from build bounds and session quotas.

    Predicates include unused declarations; facts are compiled policy facts;
    rules are normalized rules, including separate OR alternatives.
    """

    predicates: int
    facts: int
    rules: int


@dataclass(frozen=True)
class Diagnostic:
    """Native diagnostic, distinct from the operation's status code."""

    source: int = 0
    status: int = 0
    code: int = 0
    present: int = 0
    line: int = 0
    column: int = 0
    phase: str = ""
    message: str = ""
    hint: str = ""
    file: str = ""
    predicate: str = ""
    arity: int = 0
    observed_count: int = 0
    limit: int = 0
    depth: int = 0
    depth_limit: int = 0
    rule_id: int = 0
    comparison_result: int = 0
    expected_kind: int = 0
    lhs_kind: int = 0
    rhs_kind: int = 0
    comparison_op: int = 0
    limit_kind: int = 0
    term_index: int = 0
    expected_arity: int = 0
    observed_arity: int = 0
    token: str = ""
    field: str = ""
    domain: str = ""


class MaelysDatalogError(RuntimeError):
    def __init__(self, code: int, message: str, hint: str = "",
                 *, diagnostic: Diagnostic | None = None) -> None:
        self.code = code
        self.status = code
        self.message = message
        self.hint = hint
        self.diagnostic = diagnostic or Diagnostic()
        super().__init__(f"{message} ({code})" + (f": {hint}" if hint else ""))


def _text(c_string) -> str:
    return ffi.string(c_string).decode("utf-8") if c_string != ffi.NULL else ""


def _check(status: int, step: str, diagnostic=None) -> None:
    if status == lib.MAELYS_DATALOG_STATUS_OK:
        return
    detail = Diagnostic(**{
        name: _text(getattr(diagnostic, name)) if isinstance(field.default, str)
        else int(getattr(diagnostic, name))
        for name, field in Diagnostic.__dataclass_fields__.items()
    }) if diagnostic is not None else Diagnostic()
    raise MaelysDatalogError(
        int(status), detail.message or f"{step}: {_text(lib.maelys_datalog_status_name(status))}",
        detail.hint, diagnostic=detail,
    )


def _name(value: str, label: str) -> bytes:
    if not isinstance(value, str) or not value:
        raise TypeError(f"{label} must be a nonempty str")
    if "\0" in value:
        raise ValueError(f"{label} must not contain NUL")
    return value.encode("utf-8")


def _terms(values: Sequence[object]) -> tuple[object, ...]:
    if isinstance(values, (str, bytes)) or not isinstance(values, Sequence):
        raise TypeError("terms must be a sequence of values, not a string")
    if len(values) > int(lib.MAELYS_DATALOG_PUBLIC_MAX_TERMS):
        raise ValueError("too many terms for the opaque C facade")
    normalized = tuple(values)
    for value in normalized:
        if isinstance(value, str):
            if "\0" in value:
                raise ValueError("Datalog symbols must not contain NUL")
            value.encode("utf-8")
        elif isinstance(value, int):
            if not _INT64_MIN <= value <= _INT64_MAX:
                raise OverflowError("Datalog integer is outside signed int64 range")
        else:
            raise TypeError("Datalog terms must be str, int, or bool")
    return normalized


def _fill_value(slot, value: object, keepers: list[object]) -> None:
    if isinstance(value, str):
        text = ffi.new("char[]", value.encode("utf-8"))
        keepers.append(text)
        slot.kind = lib.MAELYS_DATALOG_VALUE_SYMBOL
        getattr(slot, "as").symbol = text
    elif isinstance(value, bool):
        slot.kind = lib.MAELYS_DATALOG_VALUE_BOOLEAN
        getattr(slot, "as").boolean = int(value)
    elif isinstance(value, int):
        if value < _INT64_MIN or value > _INT64_MAX:
            raise OverflowError("Datalog integer is outside signed int64 range")
        slot.kind = lib.MAELYS_DATALOG_VALUE_INTEGER
        getattr(slot, "as").integer = value
    else:
        raise TypeError("Datalog terms must be str, int, or bool")


class Engine:
    """Owns Python rulesets; native domain registration remains process-wide."""

    def __init__(self) -> None:
        if int(lib.MAELYS_DATALOG_PUBLIC_API_VERSION) != 2:
            raise RuntimeError("maelys_datalog requires Maelys Datalog consumer API 2")
        self._closed = False
        self._thread = threading.current_thread()
        self._rulesets: list[Ruleset] = []
        self.limits = Limits._read()

    def _require_open(self) -> None:
        self._require_thread()
        if self._closed:
            raise RuntimeError("Engine is closed")

    def _require_thread(self) -> None:
        if threading.current_thread() is not self._thread:
            raise RuntimeError("Engine and its handles must be used on their creating thread")

    def register_domain(
        self, name: str, predicates: Sequence[Predicate],
        *, atoms: Sequence[str] = (),
    ) -> None:
        self._require_open()
        domain_name = ffi.new("char[]", _name(name, "domain name"))
        if not predicates:
            raise ValueError("at least one predicate is required")
        declarations = ffi.new("maelys_datalog_predicate_t[]", len(predicates))
        keepers: list[object] = [domain_name, declarations]
        for index, predicate in enumerate(predicates):
            if not isinstance(predicate, Predicate):
                raise TypeError("predicates must be Predicate values")
            if isinstance(predicate.arity, bool) or not isinstance(predicate.arity, int):
                raise TypeError("predicate arity must be an int")
            predicate_name = ffi.new("char[]", _name(predicate.name, "predicate name"))
            keepers.append(predicate_name)
            declarations[index].name = predicate_name
            declarations[index].arity = predicate.arity
            declarations[index].flags = predicate.flags

        atom_buffers = [ffi.new("char[]", _name(atom, "atom")) for atom in atoms]
        atom_array = (
            ffi.new("const char *const[]", atom_buffers)
            if atom_buffers else ffi.NULL
        )
        domain = ffi.new("maelys_datalog_domain_t *")
        domain.name = domain_name
        domain.predicates = declarations
        domain.predicate_count = len(predicates)
        domain.atoms = atom_array
        domain.atom_count = len(atom_buffers)
        with _REGISTRY_LOCK:
            _check(lib.maelys_datalog_domain_register(domain), "register domain")

    def load_inline_ruleset(
        self, domain: str, policy_id: str, source: str,
    ) -> Ruleset:
        self._require_open()
        if not isinstance(source, str):
            raise TypeError("policy source must be str")
        domain_name = ffi.new("char[]", _name(domain, "domain name"))
        policy_name = ffi.new("char[]", _name(policy_id, "policy id"))
        source_bytes = source.encode("utf-8")
        source_buffer = ffi.new("char[]", source_bytes)
        out = ffi.new("maelys_datalog_policy_t **")
        diagnostic = ffi.new("maelys_datalog_diagnostic_t *")
        lib.maelys_datalog_diagnostic_init(diagnostic, ffi.sizeof("maelys_datalog_diagnostic_t"))
        with _REGISTRY_LOCK:
            _check(
                lib.maelys_datalog_policy_load_inline(
                    domain_name, policy_name, source_buffer, len(source_bytes),
                    out, diagnostic,
                ),
                "load policy", diagnostic,
            )
        ruleset = Ruleset(self, out[0])
        self._rulesets.append(ruleset)
        return ruleset

    def load_manifest(
        self, path: str | os.PathLike[str], *, allow_test_only: bool = False,
        allow_undeclared_policy_atoms: bool = False,
    ) -> Ruleset:
        """Load a native manifest atomically, including its SHA-verified policies.

        The two boolean permissions are independent and default to False.
        Enabled test_only entries fail without allow_test_only; once admitted,
        they run normally. Neither option bypasses SHA or predicate validation.
        Policy-local vocabulary never changes the global domain or inline loads.
        Paths in the manifest are resolved by the native loader.
        """
        self._require_open()
        if not isinstance(allow_test_only, bool) or not isinstance(allow_undeclared_policy_atoms, bool):
            raise TypeError("manifest options must be bool")
        flags = int(lib.MAELYS_DATALOG_PUBLIC_ALLOW_NONE)
        if allow_test_only:
            flags |= int(lib.MAELYS_DATALOG_PUBLIC_ALLOW_TEST_ONLY)
        if allow_undeclared_policy_atoms:
            flags |= int(lib.MAELYS_DATALOG_PUBLIC_ALLOW_UNDECLARED_POLICY_ATOMS)
        out = ffi.new("maelys_datalog_policy_t **")
        diagnostic = ffi.new("maelys_datalog_diagnostic_t *")
        lib.maelys_datalog_diagnostic_init(diagnostic, ffi.sizeof("maelys_datalog_diagnostic_t"))
        with _REGISTRY_LOCK:
            _check(lib.maelys_datalog_policy_load_manifest(
                _name(os.fspath(path), "manifest path"), flags, out, diagnostic,
            ), "load manifest", diagnostic)
        ruleset = Ruleset(self, out[0])
        self._rulesets.append(ruleset)
        return ruleset

    def close(self) -> None:
        self._require_thread()
        if self._closed:
            return
        for ruleset in list(self._rulesets):
            ruleset.close()
        self._rulesets.clear()
        self._closed = True

    def __enter__(self) -> Engine:
        self._require_open()
        return self

    def __exit__(self, *_exc) -> None:
        self.close()


@dataclass(frozen=True)
class SessionCapacities:
    """Fixed native E/D/S/T quotas. None means the loaded SDK's default; zero
    is an exact quota. Program-rooted symbols and NUL bytes count in S/T.
    This bounds native execution, not Python or CFFI allocations.
    """

    input_facts: int | None = None
    derived_facts: int | None = None
    symbols: int | None = None
    text_bytes: int | None = None

    def __post_init__(self) -> None:
        for name in ("input_facts", "derived_facts", "symbols", "text_bytes"):
            value = getattr(self, name)
            if value is None:
                continue
            if isinstance(value, bool) or not isinstance(value, int):
                raise TypeError(f"{name} must be an int or None")
            if not 0 <= value < (1 << (8 * ffi.sizeof("size_t"))):
                raise ValueError(f"{name} must fit size_t")


class Ruleset:
    def __init__(self, engine: Engine, policy) -> None:
        self.engine = engine
        self._policy = policy
        self._closed = False
        self._results: list[SolveResult] = []
        self._sessions: list[Session] = []
        self._edbs: list[Edb] = []

    def _require_open(self) -> None:
        self.engine._require_open()
        if self._closed:
            raise RuntimeError("Ruleset is closed")

    @property
    def policy_count(self) -> int:
        self._require_open()
        out = ffi.new("size_t *")
        _check(lib.maelys_datalog_policy_count(self._policy, out), "count policies")
        return int(out[0])

    def program_counts(self, policy_index: int = 0) -> ProgramCounts:
        """Read compiled counts without preparing a session."""
        self._require_open()
        if isinstance(policy_index, bool) or not isinstance(policy_index, int):
            raise TypeError("policy_index must be an int")
        if not 0 <= policy_index < self.policy_count:
            raise IndexError("policy_index outside the loaded policy set")
        out = ffi.new("size_t *")
        values = []
        for key in (lib.MAELYS_DATALOG_POLICY_PREDICATE_COUNT,
                    lib.MAELYS_DATALOG_POLICY_FACT_COUNT,
                    lib.MAELYS_DATALOG_POLICY_RULE_COUNT):
            _check(lib.maelys_datalog_policy_stat_get(self._policy, policy_index, key, out),
                   "read program counts")
            values.append(int(out[0]))
        return ProgramCounts(*values)

    @property
    def fingerprint(self) -> str:
        self._require_open()
        out = ffi.new("char[65]")
        _check(lib.maelys_datalog_policy_fingerprint(self._policy, out), "policy fingerprint")
        return _text(out)

    def edb(self, *, fact_capacity: int | None = None, text_capacity: int | None = None) -> Edb:
        self._require_open()
        return Edb(self, fact_capacity=fact_capacity, text_capacity=text_capacity)

    def prepare(self, policy_index: int = 0, *, required_capabilities: int = 0,
                work_limit: int = 0, explanations: ExplanationKind | int = 0,
                capacities: SessionCapacities | None = None) -> Session:
        """Prepare reusable native state for one policy (no incremental solving)."""
        self._require_open()
        if isinstance(policy_index, bool) or not isinstance(policy_index, int):
            raise TypeError("policy_index must be an int")
        if not 0 <= policy_index < self.policy_count:
            raise IndexError("policy_index outside the loaded policy set")
        for name, value in (("required_capabilities", required_capabilities), ("work_limit", work_limit)):
            if isinstance(value, bool) or not isinstance(value, int):
                raise TypeError(f"{name} must be an int")
            if not 0 <= value < (1 << 64):
                raise ValueError(f"{name} must fit uint64")
        if isinstance(explanations, bool) or not isinstance(explanations, int):
            raise TypeError("explanations must be an ExplanationKind mask")
        if explanations < 0 or explanations & ~int(ExplanationKind.TRUE | ExplanationKind.FALSE):
            raise ValueError("explanations must contain only ExplanationKind.TRUE/FALSE")
        if capacities is not None and not isinstance(capacities, SessionCapacities):
            raise TypeError("capacities must be SessionCapacities or None")
        if required_capabilities == 0 and work_limit == 0 and explanations == 0 and capacities is None:
            out = ffi.new("maelys_datalog_session_t **")
            _check(lib.maelys_datalog_session_create(self._policy, policy_index, out),
                   "create session")
            session = Session(self, out[0])
            self._sessions.append(session)
            return session
        config = ffi.new("maelys_datalog_session_config_t **")
        _check(lib.maelys_datalog_session_config_create(config), "create session configuration")
        out = ffi.new("maelys_datalog_session_t **")
        try:
            if capacities is not None:
                request = ffi.new("maelys_datalog_session_resource_request_t *")
                request.struct_size = ffi.sizeof("maelys_datalog_session_resource_request_t")
                request.contract_version = lib.MAELYS_DATALOG_RESOURCE_CONTRACT_VERSION
                request.memory_mode = lib.MAELYS_DATALOG_MEMORY_FIXED
                for bit, name in enumerate(("input_facts", "derived_facts", "symbols", "text_bytes")):
                    value = getattr(capacities, name)
                    if value is not None:
                        request.capacity_mask |= 1 << bit
                        setattr(request, name, value)
                _check(lib.maelys_datalog_session_config_set_resources(config[0], request),
                       "set fixed session capacities")
            _check(lib.maelys_datalog_session_config_set_required_capabilities(
                config[0], required_capabilities), "set required capabilities")
            _check(lib.maelys_datalog_session_config_set_work_limit(
                config[0], work_limit), "set work limit")
            _check(lib.maelys_datalog_session_config_set_explanation_workspace(
                config[0], int(explanations)), "set explanation workspace")
            _check(lib.maelys_datalog_session_create_configured(
                self._policy, policy_index, config[0], out), "create session")
        finally:
            # Native session creation snapshots values, retaining no config handle.
            lib.maelys_datalog_session_config_free(config[0])
        session = Session(self, out[0], explanations=explanations)
        self._sessions.append(session)
        return session

    def solve(self, edb: Edb, *, policy_index: int = 0,
              required_capabilities: int = 0, work_limit: int = 0,
              explanations: ExplanationKind | int = 0,
              capacities: SessionCapacities | None = None) -> SolveResult:
        """Convenience solve with a private session owned by the returned result."""
        session = self.prepare(policy_index, required_capabilities=required_capabilities,
                               work_limit=work_limit, explanations=explanations, capacities=capacities)
        try:
            result = session.solve(edb)
        except BaseException:
            session.close()
            raise
        result._owns_session = True
        return result

    def close(self) -> None:
        self.engine._require_thread()
        if self._closed:
            return
        for result in list(self._results):
            result.close()
        for session in list(self._sessions):
            session.close()
        for edb in list(self._edbs):
            edb.close()
        _check(lib.maelys_datalog_policy_free(self._policy), "free policy")
        self._policy = ffi.NULL
        self._closed = True
        self.engine._rulesets.remove(self)

    def __enter__(self) -> Ruleset:
        self._require_open()
        return self

    def __exit__(self, *_exc) -> None:
        self.close()


class Session:
    """Single-threaded prepared state with at most one live result lease."""

    def __init__(self, ruleset: Ruleset, session, *, explanations: ExplanationKind | int = 0) -> None:
        self.ruleset = ruleset
        self._session = session
        self._closed = False
        self._active: SolveResult | None = None
        self._inputs: SessionInputs | None = None
        self._busy = False
        self._explanations = ExplanationKind(explanations)

    def __del__(self) -> None:
        if not getattr(self, "_closed", True):
            warnings.warn("Unclosed Session; use close() or a context manager. "
                          "Garbage collection does not release native resources.",
                          ResourceWarning, stacklevel=2)

    def _require_open(self) -> None:
        self.ruleset._require_open()
        if self._closed:
            raise RuntimeError("Session is closed")

    @property
    def capacities(self) -> SessionCapacities:
        """Effective native quotas, queried only when explicitly requested."""
        self._require_open()
        out = ffi.new("maelys_datalog_session_resources_t *")
        out.struct_size = ffi.sizeof("maelys_datalog_session_resources_t")
        out.contract_version = lib.MAELYS_DATALOG_RESOURCE_CONTRACT_VERSION
        out.memory_mode = lib.MAELYS_DATALOG_MEMORY_FIXED
        _check(lib.maelys_datalog_session_get_resources(self._session, out), "session resources")
        return SessionCapacities(*(int(getattr(out, name)) for name in
            ("input_facts", "derived_facts", "symbols", "text_bytes")))

    @property
    def fingerprint(self) -> str:
        self._require_open()
        out = ffi.new("char[65]")
        _check(lib.maelys_datalog_session_fingerprint(self._session, out), "session fingerprint")
        return _text(out)

    @property
    def execution_fingerprint(self) -> str:
        self._require_open()
        out = ffi.new("char[65]")
        _check(lib.maelys_datalog_session_execution_fingerprint(self._session, out),
               "execution fingerprint")
        return _text(out)

    def inputs(self, *, fact_capacity: int | None = None,
               addition_capacity: int | None = None, removal_capacity: int | None = None,
               symbols: Iterable[str] = ()) -> SessionInputs:
        """Attach fixed-vocabulary retained input before the first successful solve.

        Each omitted batch capacity defaults to fact_capacity; its default is E.
        This binding allocates the aligned attachment; native transactions do not.
        """
        self._require_open()
        if self._busy or self._inputs is not None or self._active is not None:
            raise RuntimeError("Session is busy or already has retained input")
        self._busy = True
        try:
            inputs = SessionInputs(self, fact_capacity, addition_capacity, removal_capacity, symbols, _token=_INPUTS_TOKEN)
            self._inputs = inputs
            return inputs
        finally:
            self._busy = False

    def solve(self, edb: Edb) -> SolveResult:
        self._require_open()
        if self._busy:
            raise RuntimeError("Session input is being staged")
        if self._active is not None:
            raise RuntimeError("Close the current result before reusing this Session")
        if not isinstance(edb, Edb) or edb.ruleset is not self.ruleset or edb._closed:
            raise RuntimeError("EDB belongs to another or closed Ruleset")
        result_out = ffi.new("maelys_datalog_result_t **")
        diagnostic = ffi.new("maelys_datalog_diagnostic_t *")
        lib.maelys_datalog_diagnostic_init(diagnostic, ffi.sizeof("maelys_datalog_diagnostic_t"))
        _check(lib.maelys_datalog_session_solve_edb(
            self._session, edb._edb, result_out, diagnostic,
        ), "solve", diagnostic)
        edb._finalized = True
        result = SolveResult(self.ruleset, self, result_out[0])
        self._active = result
        self.ruleset._results.append(result)
        return result

    def close(self) -> None:
        self.ruleset.engine._require_thread()
        if self._closed:
            return
        if self._busy:
            raise RuntimeError("Session input is being staged")
        if self._active is not None:
            # Prevent recursion for a convenience session owned by its result.
            self._active._owns_session = False
            self._active.close()
        if self._inputs is not None:
            self._inputs.close()
        _check(lib.maelys_datalog_session_free(self._session), "free session")
        self._session = ffi.NULL
        self._closed = True
        self.ruleset._sessions.remove(self)

    def __enter__(self) -> Session:
        self._require_open()
        return self

    def __exit__(self, *_exc) -> None:
        self.close()


@dataclass(frozen=True)
class InputBase:
    """Exact uint64 attachment incarnation and committed input generation."""

    incarnation: int
    generation: int

    def __post_init__(self) -> None:
        for value in (self.incarnation, self.generation):
            if isinstance(value, bool) or not isinstance(value, int):
                raise TypeError("Input base fields must be integers")
            if not 0 <= value < (1 << 64):
                raise ValueError("Input base fields must fit uint64")


class SessionInputs:
    """Retained dynamic facts. Create with Session.inputs(); close before the session.

    replace/apply require an explicit current base and return an ordinary result
    lease. Failure preserves committed input and base. Python/CFFI staging still
    allocates; the fixed native transaction has no engine allocation fallback.
    """

    def __init__(self, session: Session, fact_capacity, addition_capacity,
                 removal_capacity, symbols, *, _token=None) -> None:
        if _token is not _INPUTS_TOKEN:
            raise TypeError("SessionInputs is created by Session.inputs()")
        self.session = session
        self._closed = True
        if fact_capacity is None:
            fact_capacity = session.capacities.input_facts
        capacities = (fact_capacity,
                      fact_capacity if addition_capacity is None else addition_capacity,
                      fact_capacity if removal_capacity is None else removal_capacity)
        for value in capacities:
            if isinstance(value, bool) or not isinstance(value, int):
                raise TypeError("Input capacities must be integers")
            if not 0 <= value < (1 << (8 * ffi.sizeof("size_t"))):
                raise ValueError("Input capacities must fit size_t")
        if isinstance(symbols, (str, bytes)):
            raise TypeError("symbols must be an iterable of strings")
        keepers = []
        for symbol in symbols:
            if len(keepers) >= session.ruleset.engine.limits.max_symbols:
                raise ValueError("Too many vocabulary entries")
            if not isinstance(symbol, str):
                raise TypeError("Vocabulary entries must be strings")
            _terms([symbol])
            keepers.append(ffi.new("char[]", symbol.encode("utf-8")))
        options = ffi.new("maelys_datalog_input_options_t *")
        options.struct_size = ffi.sizeof("maelys_datalog_input_options_t")
        options.contract_version = lib.MAELYS_DATALOG_INPUT_CONTRACT_VERSION
        options.fact_capacity, options.addition_capacity, options.removal_capacity = capacities
        pointers = ffi.new("const char *[]", keepers) if keepers else ffi.NULL
        options.symbols, options.symbol_count = pointers, len(keepers)
        size, align = ffi.new("size_t *"), ffi.new("size_t *")
        _check(lib.maelys_datalog_session_inputs_storage_requirements(
            session._session, options, size, align), "size retained input")
        owner = ffi.new("char[]", int(size[0] + align[0] - 1))
        address = int(ffi.cast("uintptr_t", owner))
        storage = ffi.cast("void *", (address + int(align[0]) - 1) & -int(align[0]))
        out = ffi.new("maelys_datalog_session_inputs_t **")
        _check(lib.maelys_datalog_session_inputs_init(
            session._session, options, storage, size[0], out), "attach retained input")
        self._storage, self._inputs = owner, out[0]
        self._capacities = capacities
        self._closed = False

    def _require_open(self) -> None:
        self.session._require_open()
        if self._closed:
            raise RuntimeError("SessionInputs is closed")

    @property
    def base(self) -> InputBase:
        self._require_open()
        out = ffi.new("maelys_datalog_input_base_t *")
        _check(lib.maelys_datalog_session_inputs_base(self._inputs, out), "read input base")
        return InputBase(int(out.incarnation), int(out.generation))

    @staticmethod
    def _stage(facts, capacity):
        staged, keepers = [], []
        for item in facts:
            if len(staged) >= capacity:
                raise MaelysDatalogError(int(Status.PAYLOAD_TOO_LARGE),
                    "Input batch exceeds its raw capacity", "No input was committed.")
            if isinstance(item, (str, bytes)) or not isinstance(item, Sequence) or len(item) != 2:
                raise TypeError("each fact must be a (predicate, terms) pair")
            predicate, terms = item
            staged.append((_name(predicate, "predicate"), _terms(terms)))
        native = ffi.new("maelys_datalog_fact_t[]", len(staged)) if staged else ffi.NULL
        for i, (predicate, terms) in enumerate(staged):
            name = ffi.new("char[]", predicate); keepers.append(name)
            native[i].predicate, native[i].arity = name, len(terms)
            for j, term in enumerate(terms):
                _fill_value(native[i].terms[j], term, keepers)
        return native, len(staged), keepers

    def replace(self, base: InputBase, facts: Iterable[tuple[str, Sequence[object]]]) -> SolveResult:
        """Replace the complete dynamic input; raw bound applies before deduplication."""
        return self._transact(base, facts, (), replace=True)

    def apply(self, base: InputBase, *, added: Iterable[tuple[str, Sequence[object]]] = (),
              removed: Iterable[tuple[str, Sequence[object]]] = ()) -> SolveResult:
        """Apply a supplied delta atomically. Additions win; absent removals do nothing."""
        return self._transact(base, added, removed, replace=False)

    def _transact(self, base, added, removed, *, replace):
        self._require_open()
        session = self.session
        if session._busy or session._active is not None:
            raise RuntimeError("Close the current result before reusing this Session")
        if not isinstance(base, InputBase):
            raise TypeError("base must be InputBase")
        session._busy = True
        try:
            a, na, owners_a = self._stage(added, self._capacities[0 if replace else 1])
            r, nr, owners_r = self._stage(removed, self._capacities[2])
            self._require_open()
            native_base = ffi.new("maelys_datalog_input_base_t *", (base.incarnation, base.generation))
            result_out = ffi.new("maelys_datalog_result_t **")
            diagnostic = ffi.new("maelys_datalog_diagnostic_t *")
            lib.maelys_datalog_diagnostic_init(diagnostic, ffi.sizeof("maelys_datalog_diagnostic_t"))
            if replace:
                status = lib.maelys_datalog_session_inputs_replace(
                    self._inputs, native_base[0], a, na, result_out, diagnostic)
            else:
                status = lib.maelys_datalog_session_inputs_apply(
                    self._inputs, native_base[0], a, na, r, nr, result_out, diagnostic)
            # Keep borrowed text alive across the complete native call.
            del owners_a, owners_r
            _check(status, "replace inputs" if replace else "apply inputs", diagnostic)
            result = SolveResult(session.ruleset, session, result_out[0])
            session._active = result
            session.ruleset._results.append(result)
            return result
        finally:
            session._busy = False

    def close(self) -> None:
        self.session.ruleset.engine._require_thread()
        if self._closed:
            return
        if self.session._busy:
            raise RuntimeError("Session input is being staged")
        _check(lib.maelys_datalog_session_inputs_free(self._inputs), "release retained input")
        self._inputs = ffi.NULL
        self._closed = True
        self.session._inputs = None
        ffi.release(self._storage)

    def __enter__(self) -> SessionInputs:
        self._require_open()
        return self

    def __exit__(self, *_exc) -> None:
        self.close()


class Edb:
    """Owned native input buffer. Domain validation remains a solve operation."""

    def __init__(self, ruleset: Ruleset, *, fact_capacity: int | None = None,
                 text_capacity: int | None = None) -> None:
        ruleset._require_open()
        self.ruleset = ruleset
        limits = ruleset.engine.limits
        if fact_capacity is None:
            fact_capacity = limits.max_edb_facts
        if isinstance(fact_capacity, bool) or not isinstance(fact_capacity, int):
            raise TypeError("fact_capacity must be an int")
        if not 1 <= fact_capacity <= limits.max_edb_facts:
            raise ValueError(f"fact_capacity must be between 1 and {limits.max_edb_facts}")
        if text_capacity is None:
            text_capacity = limits.input_edb_text_bytes
        if isinstance(text_capacity, bool) or not isinstance(text_capacity, int):
            raise TypeError("text_capacity must be an int")
        if not 0 <= text_capacity <= limits.input_edb_text_bytes:
            raise ValueError(f"text_capacity must be between 0 and {limits.input_edb_text_bytes}")
        out = ffi.new("maelys_datalog_input_edb_t **")
        _check(lib.maelys_datalog_input_edb_create_with_capacity(
            fact_capacity, text_capacity, out), "create input EDB")
        self._edb = out[0]
        self._fact_capacity = fact_capacity
        self._closed = False
        self._finalized = False
        ruleset._edbs.append(self)

    def __del__(self) -> None:
        if not getattr(self, "_closed", True):
            warnings.warn("Unclosed Edb; use close() or close its owner. "
                          "Garbage collection does not release native resources.",
                          ResourceWarning, stacklevel=2)

    def __len__(self) -> int:
        self.ruleset._require_open()
        if self._closed:
            raise RuntimeError("EDB is closed")
        out = ffi.new("size_t *")
        _check(lib.maelys_datalog_input_edb_count(self._edb, out), "count input entries")
        return int(out[0])

    def _require_mutable(self) -> None:
        self.ruleset._require_open()
        if self._closed or self.ruleset._closed or self._finalized:
            raise RuntimeError("EDB is closed for mutation")

    def add_fact(self, predicate: str, terms: Sequence[object]) -> None:
        """Copy one fact into C-owned storage; domain checks occur at solve."""
        self._require_mutable()
        name = _name(predicate, "predicate")
        normalized = _terms(terms)
        native = ffi.new("maelys_datalog_value_t[]", len(normalized)) if normalized else ffi.NULL
        keepers: list[object] = []
        for index, value in enumerate(normalized):
            _fill_value(native[index], value, keepers)
        self._require_mutable()
        diagnostic = ffi.new("maelys_datalog_diagnostic_t *")
        lib.maelys_datalog_diagnostic_init(diagnostic, ffi.sizeof("maelys_datalog_diagnostic_t"))
        _check(lib.maelys_datalog_input_edb_add_fact(
            self._edb, name, native, len(normalized), diagnostic), "add input fact", diagnostic)

    def add_facts(self, facts: Iterable[tuple[str, Sequence[object]]]) -> None:
        """Buffer an iterable atomically: validation/iteration failure adds nothing.

        Staging is bounded by this EDB's actual fact capacity. At most one
        extra item is consumed to detect overflow, before any native call.
        Remaining native capacity is checked atomically at append.
        After success only the native EDB retains the input values.
        """
        self._require_mutable()
        staged: list[tuple[str, tuple[object, ...]]] = []
        for item in facts:
            if len(staged) >= self._fact_capacity:
                raise MaelysDatalogError(
                    int(lib.MAELYS_DATALOG_STATUS_PAYLOAD_TOO_LARGE),
                    "Input batch exceeds the EDB fact limit (before deduplication)",
                    "No facts from this batch were appended; reduce the batch and retry.")
            if isinstance(item, (str, bytes)) or not isinstance(item, Sequence) or len(item) != 2:
                raise TypeError("each fact must be a (predicate, terms) pair")
            predicate, terms = item
            _name(predicate, "predicate")
            staged.append((predicate, _terms(terms)))
        # Iterators can execute caller code, including closing this EDB.
        self._require_mutable()
        native = ffi.new("maelys_datalog_fact_t[]", len(staged)) if staged else ffi.NULL
        keepers: list[object] = []
        for index, (predicate, terms) in enumerate(staged):
            name = ffi.new("char[]", _name(predicate, "predicate"))
            keepers.append(name)
            native[index].predicate = name
            native[index].arity = len(terms)
            for term_index, term in enumerate(terms):
                _fill_value(native[index].terms[term_index], term, keepers)
        self._require_mutable()
        diagnostic = ffi.new("maelys_datalog_diagnostic_t *")
        lib.maelys_datalog_diagnostic_init(diagnostic, ffi.sizeof("maelys_datalog_diagnostic_t"))
        _check(lib.maelys_datalog_input_edb_add_facts(
            self._edb, native, len(staged), diagnostic), "add input batch", diagnostic)

    def clear(self) -> None:
        """Clear an unsolved buffer, for example after a domain-validation error."""
        self._require_mutable()
        _check(lib.maelys_datalog_input_edb_clear(self._edb), "clear input EDB")

    def reset(self) -> None:
        """Explicitly start a new input batch in the same native storage.

        Existing results stay valid; their session's one-result lease remains.
        Unlike clear(), this explicitly ends the successful-solve mutation freeze.
        """
        self.ruleset._require_open()
        if self._closed:
            raise RuntimeError("EDB is closed")
        _check(lib.maelys_datalog_input_edb_clear(self._edb), "reset input EDB")
        self._finalized = False

    def close(self) -> None:
        self.ruleset.engine._require_thread()
        if self._closed:
            return
        _check(lib.maelys_datalog_input_edb_free(self._edb), "free input EDB")
        self._edb = ffi.NULL
        self._closed = True
        self.ruleset._edbs.remove(self)


@dataclass(frozen=True)
class ResultTerm:
    """Raw term view. Symbol IDs are valid only within the owning live result."""

    kind: str
    value: int | bool
    _owner: SolveResult = field(repr=False)

    def resolve(self) -> object:
        return self._owner.resolve_term(self)


class SolveResult:
    def __init__(self, ruleset: Ruleset, session, result) -> None:
        self.ruleset = ruleset
        self._session = session
        self._result = result
        self._closed = False
        self._owns_session = False

    def __del__(self) -> None:
        if not getattr(self, "_closed", True):
            warnings.warn("Unclosed SolveResult; use close() or a context manager. "
                          "Garbage collection does not release native resources.",
                          ResourceWarning, stacklevel=2)

    def _require_open(self) -> None:
        self.ruleset._require_open()
        if self._closed or self.ruleset._closed:
            raise RuntimeError("SolveResult is closed")

    @property
    def fingerprint(self) -> str:
        self._require_open()
        return self._session.fingerprint

    @property
    def execution_fingerprint(self) -> str:
        self._require_open()
        return self._session.execution_fingerprint

    def explain_true(self, predicate: str, terms: Sequence[object]) -> str:
        """Prepare Why-true once, copy its text, then release its result lease."""
        return self._explain(lib.MAELYS_DATALOG_EXPLAIN_TRUE, predicate, terms)

    def explain_false(self, predicate: str, terms: Sequence[object]) -> str:
        """Return bounded Why-false text, including its completeness status.

        Truncated output is not a proof of non-derivability. Unknown symbols
        and missing backend capabilities are native errors, not false answers.
        """
        return self._explain(lib.MAELYS_DATALOG_EXPLAIN_FALSE, predicate, terms)

    def _explain(self, kind, predicate: str, terms: Sequence[object]) -> str:
        self._require_open()
        name = _name(predicate, "predicate")
        normalized = _terms(terms)
        values = ffi.new("maelys_datalog_value_t[]", len(normalized)) if normalized else ffi.NULL
        keepers: list[object] = []
        for index, term in enumerate(normalized):
            _fill_value(values[index], term, keepers)
        if self._session._explanations:
            # Both calls address the same native one-entry cache. No CFFI arena
            # per explanation; conversion, output and Python strings still allocate.
            explain = (lib.maelys_datalog_result_explain_true_text
                       if kind == lib.MAELYS_DATALOG_EXPLAIN_TRUE
                       else lib.maelys_datalog_result_explain_false_text)
            required = ffi.new("size_t *")
            _check(explain(self._result, name, values, len(normalized),
                           ffi.NULL, 0, required), "prepare cached explanation")
            text = ffi.new("char[]", int(required[0]) + 1)
            _check(explain(self._result, name, values, len(normalized),
                           text, int(required[0]) + 1, required), "render cached explanation")
            return bytes(ffi.buffer(text, required[0])).decode("utf-8")
        storage_bytes = ffi.new("size_t *")
        alignment = ffi.new("size_t *")
        _check(lib.maelys_datalog_result_explanation_storage_requirements(
            self._result, kind, storage_bytes, alignment), "size explanation workspace")
        size, align = int(storage_bytes[0]), int(alignment[0])
        if size <= 0 or align <= 0 or align & (align - 1):
            raise RuntimeError("Native explanation workspace has invalid size or alignment")
        # Keep the owning cdata alive until release; an aligned cast does not own
        # this allocation. No native heap fallback is used by prepare/write.
        storage_owner = ffi.new("unsigned char[]", size + align - 1)
        address = int(ffi.cast("uintptr_t", storage_owner))
        storage = ffi.cast("void *", (address + align - 1) & ~(align - 1))
        prepared = ffi.new("maelys_datalog_prepared_explanation_t **")
        try:
            _check(lib.maelys_datalog_result_prepare_explanation(
                self._result, kind, name, values, len(normalized),
                storage, size, prepared), "prepare explanation")
            required = ffi.new("size_t *")
            _check(lib.maelys_datalog_prepared_explanation_text_size(prepared[0], required),
                   "read explanation text size")
            capacity = int(required[0]) + 1  # Native required length excludes NUL.
            text = ffi.new("char[]", capacity)
            _check(lib.maelys_datalog_prepared_explanation_write_text(prepared[0], text, capacity),
                   "render prepared explanation")
            return bytes(ffi.buffer(text, required[0])).decode("utf-8")
        finally:
            if prepared[0] != ffi.NULL:
                _check(lib.maelys_datalog_prepared_explanation_release(prepared[0]),
                       "release explanation")
            # An explicit use/release also keeps the original allocation alive
            # on Python implementations that can collect dead locals early.
            ffi.release(storage_owner)

    def contains_fact(self, predicate: str, terms: Sequence[object]) -> bool:
        self._require_open()
        name = ffi.new("char[]", _name(predicate, "predicate"))
        normalized = _terms(terms)
        values = (
            ffi.new("maelys_datalog_value_t[]", len(normalized))
            if normalized else ffi.NULL
        )
        keepers: list[object] = [name]
        for index, term in enumerate(normalized):
            _fill_value(values[index], term, keepers)
        answer = ffi.new("int *")
        _check(
            lib.maelys_datalog_result_query(
                self._result, name, values, len(normalized), answer,
            ),
            "query",
        )
        return bool(answer[0])

    def derived_fact_count(self) -> int:
        """Count all distinct derived facts, including non-queryable predicates."""
        self._require_open()
        count = ffi.new("size_t *")
        _check(
            lib.maelys_datalog_result_derived_fact_count(self._result, count),
            "count all derived facts",
        )
        return int(count[0])

    def enumerate_predicate_facts(
        self, predicate: str, arity: int,
    ) -> list[tuple[object, ...]]:
        """Return Python values copied from the result (safe after close)."""
        return [tuple(term.resolve() for term in row)
                for row in self.enumerate_raw(predicate, arity)]

    def enumerate_raw(self, predicate: str, arity: int) -> list[tuple[ResultTerm, ...]]:
        """Return typed, result-bound views; never ruleset-scoped symbol IDs."""
        self._require_open()
        if isinstance(arity, bool) or not isinstance(arity, int):
            raise TypeError("arity must be an int")
        if arity < 0 or arity > int(lib.MAELYS_DATALOG_PUBLIC_MAX_TERMS):
            raise ValueError("arity outside the opaque C facade limit")
        name = ffi.new("char[]", _name(predicate, "predicate"))
        count = ffi.new("size_t *")
        _check(
            lib.maelys_datalog_result_enumerate(
                self._result, name, arity, ffi.NULL, 0, count,
            ),
            "count derived facts",
        )
        if count[0] == 0:
            return []
        views = ffi.new("maelys_datalog_fact_view_t[]", count[0])
        _check(
            lib.maelys_datalog_result_enumerate(
                self._result, name, arity, views, count[0], count,
            ),
            "enumerate derived facts",
        )
        resolved: list[tuple[ResultTerm, ...]] = []
        for fact in views[0 : count[0]]:
            row: list[ResultTerm] = []
            for term in fact.terms[0:arity]:
                if term.kind == lib.MAELYS_DATALOG_VALUE_SYMBOL:
                    row.append(ResultTerm("symbol", int(getattr(term, "as").symbol_id), self))
                elif term.kind == lib.MAELYS_DATALOG_VALUE_INTEGER:
                    row.append(ResultTerm("integer", int(getattr(term, "as").integer), self))
                elif term.kind == lib.MAELYS_DATALOG_VALUE_BOOLEAN:
                    row.append(ResultTerm("boolean", bool(getattr(term, "as").boolean), self))
                else:
                    raise RuntimeError("unknown result term kind")
            resolved.append(tuple(row))
        return resolved

    def resolve_term(self, term: ResultTerm) -> object:
        self._require_open()
        if not isinstance(term, ResultTerm) or term._owner is not self:
            raise ValueError("Term belongs to another result")
        if term.kind == "symbol":
            text = ffi.new("const char **")
            length = ffi.new("size_t *")
            _check(lib.maelys_datalog_result_symbol_text(
                self._result, term.value, text, length,
            ), "resolve result symbol")
            return bytes(ffi.buffer(text[0], length[0])).decode("utf-8")
        if term.kind not in ("integer", "boolean"):
            raise ValueError("Unknown result term kind")
        return term.value

    def close(self) -> None:
        self.ruleset.engine._require_thread()
        if self._closed:
            return
        _check(lib.maelys_datalog_result_free(self._result), "free result")
        self._result = ffi.NULL
        self._closed = True
        self._session._active = None
        if self._owns_session:
            self._session.close()
        if self in self.ruleset._results:
            self.ruleset._results.remove(self)

    def __enter__(self) -> SolveResult:
        self._require_open()
        return self

    def __exit__(self, *_exc) -> None:
        self.close()
