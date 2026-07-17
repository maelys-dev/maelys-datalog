from __future__ import annotations

import os
import platform
import sysconfig
from pathlib import Path

from cffi import FFI


ROOT = Path(__file__).resolve().parents[2]
PACKAGE_DIR = Path(__file__).resolve().parent / "maelys_datalog"


def _split_flags(value: str | None) -> list[str]:
    return [part for part in (value or "").split() if part]


ffibuilder = FFI()

ffibuilder.cdef(
    """
typedef struct maelys_py_engine maelys_py_engine_t;
typedef struct maelys_py_ruleset maelys_py_ruleset_t;
typedef struct maelys_py_edb maelys_py_edb_t;
typedef struct maelys_py_result maelys_py_result_t;

typedef struct {
    size_t max_symbols;
    size_t string_pool_bytes;
    size_t max_predicates;
    size_t max_rules;
    size_t max_arity;
    size_t max_body_literals;
    size_t max_depth;
    size_t max_edb_facts;
    size_t max_idb_facts;
    size_t max_facts_per_pred;
} maelys_datalog_build_limits_t;

typedef struct { int32_t kind; int64_t value; } maelys_py_term_t;
typedef struct { const char *name; size_t arity; unsigned kind_flags; } maelys_py_predicate_def_t;

typedef struct {
    size_t term_size, term_kind_offset, term_value_offset;
    size_t predicate_def_size, predicate_def_name_offset,
           predicate_def_arity_offset, predicate_def_kind_flags_offset;
} maelys_py_abi_layout_t;

typedef struct {
    int32_t term_symbol, term_int, term_bool, term_var;
    unsigned pred_edb, pred_idb, pred_query, pred_policy_fact;
    int ok, err_invalid_argument, err_invalid_field,
        err_invalid_state, err_payload_too_large,
        err_forbidden, err_unsupported;
} maelys_py_abi_constants_t;

maelys_py_engine_t *maelys_py_engine_new(void);
void maelys_py_engine_free(maelys_py_engine_t *engine);
int maelys_py_get_build_limits(maelys_datalog_build_limits_t *out);
void maelys_py_get_abi_layout(maelys_py_abi_layout_t *out);
void maelys_py_get_abi_constants(maelys_py_abi_constants_t *out);
int maelys_py_register_domain(const char *domain_name,
                              const maelys_py_predicate_def_t *predicates,
                              size_t predicate_count);
int maelys_py_find_domain(const char *domain_name,
                          maelys_py_predicate_def_t *out_predicates,
                          size_t out_capacity,
                          size_t *out_count,
                          int *out_found,
                          int *out_inspectable);
int maelys_py_load_inline_ruleset(maelys_py_engine_t *engine,
                                  const char *domain_name,
                                  const char *ruleset_id,
                                  const char *source,
                                  size_t source_len,
                                  maelys_py_ruleset_t **out_ruleset);
const char *maelys_py_last_diag_message(maelys_py_engine_t *engine);
const char *maelys_py_last_diag_hint(maelys_py_engine_t *engine);
void maelys_py_ruleset_free(maelys_py_ruleset_t *ruleset);
maelys_py_edb_t *maelys_py_edb_new(maelys_py_ruleset_t *ruleset);
void maelys_py_edb_free(maelys_py_edb_t *edb);
int maelys_py_edb_add_fact(maelys_py_edb_t *edb,
                           const char *predicate,
                           const maelys_py_term_t *terms,
                           size_t arity);
int maelys_py_intern_symbol(maelys_py_ruleset_t *ruleset,
                            const char *text,
                            uint32_t *out_symbol_id);
const char *maelys_py_symbol_text(maelys_py_ruleset_t *ruleset,
                                  uint32_t symbol_id);
int maelys_py_solve(maelys_py_ruleset_t *ruleset,
                    maelys_py_edb_t *edb,
                    maelys_py_result_t **out_result);
void maelys_py_result_free(maelys_py_result_t *result);
int maelys_py_result_derived_fact_count(maelys_py_result_t *result,
                                        size_t *out_count);
int maelys_py_result_enumerate_predicate_facts(maelys_py_result_t *result,
                                               const char *predicate,
                                               size_t arity,
                                               maelys_py_term_t *out_terms,
                                               size_t out_capacity,
                                               size_t *out_count);
"""
)

if platform.system() == "Darwin":
    rpath_arg = "-Wl,-rpath,@loader_path"
else:
    rpath_arg = "-Wl,-rpath,$ORIGIN"

ffibuilder.set_source(
    "maelys_datalog._maelys_cffi",
    '#include "bindings/python/maelys_py_bind.h"',
    include_dirs=[str(ROOT)],
    library_dirs=[str(PACKAGE_DIR)],
    libraries=["maelys_py_bind"],
    extra_compile_args=_split_flags(os.environ.get("MAELYS_PY_CFLAGS")),
    extra_link_args=[rpath_arg] + _split_flags(os.environ.get("MAELYS_PY_LDFLAGS")),
)


if __name__ == "__main__":
    ext_suffix = sysconfig.get_config_var("EXT_SUFFIX") or ".so"
    ffibuilder.compile(
        tmpdir=str(Path(__file__).resolve().parent / "build"),
        target=str(PACKAGE_DIR / ("_maelys_cffi" + ext_suffix)),
        verbose=True,
    )
