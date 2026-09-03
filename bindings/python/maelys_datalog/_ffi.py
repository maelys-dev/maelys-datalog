from __future__ import annotations

from types import SimpleNamespace

from ._maelys_cffi import ffi, lib


def _check_abi_layout() -> None:
    layout = ffi.new("maelys_py_abi_layout_t *")
    lib.maelys_py_get_abi_layout(layout)
    expected = {
        "term_size": ffi.sizeof("maelys_py_term_t"),
        "term_kind_offset": ffi.offsetof("maelys_py_term_t", "kind"),
        "term_value_offset": ffi.offsetof("maelys_py_term_t", "value"),
        "predicate_def_size": ffi.sizeof("maelys_py_predicate_def_t"),
        "predicate_def_name_offset": ffi.offsetof("maelys_py_predicate_def_t", "name"),
        "predicate_def_arity_offset": ffi.offsetof("maelys_py_predicate_def_t", "arity"),
        "predicate_def_kind_flags_offset": ffi.offsetof(
            "maelys_py_predicate_def_t", "kind_flags"
        ),
    }
    for field, value in expected.items():
        if getattr(layout, field) != value:
            raise ImportError(
                f"maelys_datalog cffi ABI layout mismatch for {field}: "
                f"Python={value} C={getattr(layout, field)}"
            )


def _load_constants() -> SimpleNamespace:
    constants = ffi.new("maelys_py_abi_constants_t *")
    lib.maelys_py_get_abi_constants(constants)
    return SimpleNamespace(
        TERM_SYMBOL=constants.term_symbol,
        TERM_INT=constants.term_int,
        TERM_BOOL=constants.term_bool,
        TERM_VAR=constants.term_var,
        PRED_EDB=constants.pred_edb,
        PRED_IDB=constants.pred_idb,
        PRED_QUERY=constants.pred_query,
        PRED_POLICY_FACT=constants.pred_policy_fact,
        OK=constants.ok,
        ERR_INVALID_ARGUMENT=constants.err_invalid_argument,
        ERR_INVALID_FIELD=constants.err_invalid_field,
        ERR_INVALID_STATE=constants.err_invalid_state,
        ERR_PAYLOAD_TOO_LARGE=constants.err_payload_too_large,
        ERR_FORBIDDEN=constants.err_forbidden,
        ERR_UNSUPPORTED=constants.err_unsupported,
    )


_check_abi_layout()
C = _load_constants()
