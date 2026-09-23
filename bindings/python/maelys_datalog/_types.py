from __future__ import annotations

from dataclasses import dataclass
from typing import TypeAlias

from ._ffi import C


@dataclass(frozen=True)
class Predicate:
    name: str
    arity: int
    kind_flags: int


@dataclass(frozen=True)
class BuildLimits:
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


@dataclass(frozen=True)
class Term:
    kind: int
    value: int

    @staticmethod
    def symbol_id(symbol_id: int) -> "Term":
        return Term(C.TERM_SYMBOL, int(symbol_id))

    @staticmethod
    def integer(value: int) -> "Term":
        return Term(C.TERM_INT, int(value))

    @staticmethod
    def boolean(value: bool) -> "Term":
        return Term(C.TERM_BOOL, 1 if value else 0)


InputTerm: TypeAlias = str | int | bool | Term
ResolvedTerm: TypeAlias = str | int | bool
Fact: TypeAlias = tuple[ResolvedTerm, ...]
RawFact: TypeAlias = tuple[Term, ...]
