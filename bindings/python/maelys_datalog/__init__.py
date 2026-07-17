from __future__ import annotations

# Import _ffi first so ABI layout and constants are checked at package import.
from . import _ffi as _ffi
from ._ffi import C
from ._types import BuildLimits, Predicate, Term
from .engine import Edb, Engine, Ruleset, SolveResult
from .errors import (
    DomainAlreadyRegisteredError,
    DomainRegistryFullError,
    MaelysDatalogError,
)


TERM_SYMBOL = C.TERM_SYMBOL
TERM_INT = C.TERM_INT
TERM_BOOL = C.TERM_BOOL
TERM_VAR = C.TERM_VAR

PRED_EDB = C.PRED_EDB
PRED_IDB = C.PRED_IDB
PRED_QUERY = C.PRED_QUERY
PRED_POLICY_FACT = C.PRED_POLICY_FACT


__all__ = [
    "BuildLimits",
    "C",
    "DomainAlreadyRegisteredError",
    "DomainRegistryFullError",
    "Edb",
    "Engine",
    "MaelysDatalogError",
    "PRED_EDB",
    "PRED_IDB",
    "PRED_POLICY_FACT",
    "PRED_QUERY",
    "Predicate",
    "Ruleset",
    "SolveResult",
    "TERM_BOOL",
    "TERM_INT",
    "TERM_SYMBOL",
    "TERM_VAR",
    "Term",
]
