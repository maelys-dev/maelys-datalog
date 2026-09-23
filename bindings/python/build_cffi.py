"""Build maelys_datalog against an installed public SDK (consumer API 2)."""

from __future__ import annotations

import argparse
import platform
import shutil
import sysconfig
import tempfile
from pathlib import Path

from cffi import FFI


PACKAGE = Path(__file__).resolve().parent / "maelys_datalog"


def build(sdk_prefix: Path) -> None:
    header = sdk_prefix / "include/maelys/datalog.h"
    if not header.is_file():
        raise SystemExit(f"Missing public facade header: {header}")
    suffix = ".dylib" if platform.system() == "Darwin" else ".so"
    library_name = "libmaelys_datalog_shared" + suffix
    candidates = [sdk_prefix / directory / library_name for directory in ("lib", "lib64")]
    libraries = [path for path in candidates if path.is_file()]
    if len(libraries) != 1:
        raise SystemExit(f"Expected one installed {library_name} in {sdk_prefix}/lib or lib64")
    library = libraries[0]
    PACKAGE.mkdir(parents=True, exist_ok=True)
    # Replace the inode: overwriting a loaded Mach-O can leave stale signature
    # pages when testing SMALL/LARGE builds in successive Python processes.
    with tempfile.TemporaryDirectory(prefix=".native-", dir=PACKAGE) as staging:
        staged = Path(staging) / library_name
        shutil.copy2(library, staged)
        staged.replace(PACKAGE / library_name)

    builder = FFI()
    builder.cdef(
        """
#define MAELYS_DATALOG_PUBLIC_API_VERSION ...
#define MAELYS_DATALOG_PUBLIC_MAX_TERMS ...
#define MAELYS_DATALOG_PUBLIC_ALLOW_NONE ...
#define MAELYS_DATALOG_PUBLIC_ALLOW_TEST_ONLY ...
#define MAELYS_DATALOG_PUBLIC_ALLOW_UNDECLARED_POLICY_ATOMS ...
#define MAELYS_DATALOG_CAP_POSITIVE ...
#define MAELYS_DATALOG_CAP_NEGATION ...
#define MAELYS_DATALOG_CAP_COMPARISONS ...
#define MAELYS_DATALOG_CAP_ARITHMETIC ...
#define MAELYS_DATALOG_CAP_FILTERS ...
#define MAELYS_DATALOG_CAP_EXPLAIN_TRUE ...
#define MAELYS_DATALOG_CAP_WORK_LIMIT ...
#define MAELYS_DATALOG_CAP_EXPLAIN_FALSE ...
#define MAELYS_DATALOG_CAP_AGGREGATES ...
#define MAELYS_DATALOG_CAP_MIN ...
#define MAELYS_DATALOG_CAP_MAX ...
#define MAELYS_DATALOG_CAP_SUM ...
#define MAELYS_DATALOG_VALUE_SYMBOL ...
#define MAELYS_DATALOG_VALUE_INTEGER ...
#define MAELYS_DATALOG_VALUE_BOOLEAN ...
#define MAELYS_DATALOG_PREDICATE_EDB ...
#define MAELYS_DATALOG_PREDICATE_IDB ...
#define MAELYS_DATALOG_PREDICATE_QUERY ...
#define MAELYS_DATALOG_PREDICATE_POLICY_FACT ...
#define MAELYS_DATALOG_LIMIT_MAX_SYMBOLS ...
#define MAELYS_DATALOG_LIMIT_STRING_POOL_BYTES ...
#define MAELYS_DATALOG_LIMIT_MAX_PREDICATES ...
#define MAELYS_DATALOG_LIMIT_MAX_RULES ...
#define MAELYS_DATALOG_LIMIT_MAX_ARITY ...
#define MAELYS_DATALOG_LIMIT_MAX_BODY_LITERALS ...
#define MAELYS_DATALOG_LIMIT_MAX_DEPTH ...
#define MAELYS_DATALOG_LIMIT_MAX_EDB_FACTS ...
#define MAELYS_DATALOG_LIMIT_MAX_IDB_FACTS ...
#define MAELYS_DATALOG_LIMIT_MAX_FACTS_PER_PRED ...
#define MAELYS_DATALOG_LIMIT_MAX_STRING_BYTES ...
#define MAELYS_DATALOG_LIMIT_INPUT_EDB_TEXT_BYTES ...

#define MAELYS_DATALOG_STATUS_OK ...
#define MAELYS_DATALOG_STATUS_INVALID_ARGUMENT ...
#define MAELYS_DATALOG_STATUS_INVALID_FIELD ...
#define MAELYS_DATALOG_STATUS_NOT_FOUND ...
#define MAELYS_DATALOG_STATUS_NOT_IMPLEMENTED ...
#define MAELYS_DATALOG_STATUS_UNSUPPORTED ...
#define MAELYS_DATALOG_STATUS_TIMEOUT ...
#define MAELYS_DATALOG_STATUS_IO ...
#define MAELYS_DATALOG_STATUS_INTERNAL ...
#define MAELYS_DATALOG_STATUS_UNAUTHORIZED ...
#define MAELYS_DATALOG_STATUS_FORBIDDEN ...
#define MAELYS_DATALOG_STATUS_RATE_LIMITED ...
#define MAELYS_DATALOG_STATUS_PAYLOAD_TOO_LARGE ...
#define MAELYS_DATALOG_STATUS_INVALID_STATE ...
#define MAELYS_DATALOG_STATUS_STORAGE_TOO_SMALL ...

typedef struct maelys_datalog_policy maelys_datalog_policy_t;
typedef struct maelys_datalog_session maelys_datalog_session_t;
typedef struct maelys_datalog_result maelys_datalog_result_t;
typedef struct maelys_datalog_session_config maelys_datalog_session_config_t;
typedef struct maelys_datalog_input_edb maelys_datalog_input_edb_t;
typedef struct maelys_datalog_prepared_explanation maelys_datalog_prepared_explanation_t;
typedef enum {
    MAELYS_DATALOG_EXPLAIN_TRUE = 1,
    MAELYS_DATALOG_EXPLAIN_FALSE = 2
} maelys_datalog_explanation_kind_t;
int maelys_datalog_session_config_create(maelys_datalog_session_config_t **);
int maelys_datalog_session_config_set_required_capabilities(maelys_datalog_session_config_t *, uint64_t);
int maelys_datalog_session_config_get_required_capabilities(const maelys_datalog_session_config_t *, uint64_t *);
int maelys_datalog_session_config_set_work_limit(maelys_datalog_session_config_t *, uint64_t);
int maelys_datalog_session_config_get_work_limit(const maelys_datalog_session_config_t *, uint64_t *);
int maelys_datalog_session_config_set_explanation_workspace(maelys_datalog_session_config_t *, unsigned);
int maelys_datalog_session_config_set_explanation_storage(maelys_datalog_session_config_t *, unsigned, void *, size_t);
int maelys_datalog_session_config_free(maelys_datalog_session_config_t *);
int maelys_datalog_session_create_configured(
    const maelys_datalog_policy_t *, size_t,
    const maelys_datalog_session_config_t *, maelys_datalog_session_t **);
int maelys_datalog_session_execution_fingerprint(const maelys_datalog_session_t *, char[65]);
typedef struct {
    size_t struct_size;
    unsigned int abi_version;
    int source;
    int status;
    int code;
    size_t line;
    size_t column;
    char phase[32];
    char message[256];
    char hint[256];
    uint64_t present;
    char file[256], predicate[96];
    size_t arity, observed_count, limit, depth, depth_limit, rule_id;
    unsigned int comparison_result, expected_kind, lhs_kind, rhs_kind, comparison_op;
    int limit_kind;
    size_t term_index, expected_arity, observed_arity;
    char token[96], field[96], domain[96];
    ...;
} maelys_datalog_diagnostic_t;
int maelys_datalog_diagnostic_init(void *, size_t);
const char *maelys_datalog_diag_code_name(int);
typedef struct {
    const char *name;
    size_t arity;
    unsigned flags;
} maelys_datalog_predicate_t;
typedef struct {
    const char *name;
    const maelys_datalog_predicate_t *predicates;
    size_t predicate_count;
    const char *const *atoms;
    size_t atom_count;
} maelys_datalog_domain_t;
typedef struct {
    int kind;
    union {
        const char *symbol;
        int64_t integer;
        int boolean;
    } as;
} maelys_datalog_value_t;
typedef struct {
    const char *predicate;
    size_t arity;
    maelys_datalog_value_t terms[4];
} maelys_datalog_fact_t;
typedef struct {
    int kind;
    union {
        uint32_t symbol_id;
        int64_t integer;
        int boolean;
    } as;
} maelys_datalog_term_view_t;
typedef struct {
    size_t arity;
    maelys_datalog_term_view_t terms[4];
} maelys_datalog_fact_view_t;

const char *maelys_datalog_status_name(int status);
int maelys_datalog_limit_get(int limit, size_t *out_value);
int maelys_datalog_diagnostic_clear(
    maelys_datalog_diagnostic_t *diagnostic);
int maelys_datalog_domain_register(
    const maelys_datalog_domain_t *domain);
int maelys_datalog_policy_load_inline(
    const char *domain, const char *policy_id,
    const char *source, size_t source_length,
    maelys_datalog_policy_t **out_policy,
    maelys_datalog_diagnostic_t *out_diagnostic);
int maelys_datalog_policy_free(maelys_datalog_policy_t *policy);
int maelys_datalog_policy_load_manifest(
    const char *, unsigned, maelys_datalog_policy_t **,
    maelys_datalog_diagnostic_t *);
int maelys_datalog_policy_count(const maelys_datalog_policy_t *, size_t *);
int maelys_datalog_policy_fingerprint(const maelys_datalog_policy_t *, char[65]);
int maelys_datalog_session_fingerprint(const maelys_datalog_session_t *, char[65]);
int maelys_datalog_session_create(
    const maelys_datalog_policy_t *policy, size_t policy_index,
    maelys_datalog_session_t **out_session);
int maelys_datalog_session_solve(
    maelys_datalog_session_t *session,
    const maelys_datalog_fact_t *facts, size_t fact_count,
    maelys_datalog_result_t **out_result,
    maelys_datalog_diagnostic_t *out_diagnostic);
int maelys_datalog_session_free(maelys_datalog_session_t *session);
int maelys_datalog_input_edb_create(maelys_datalog_input_edb_t **);
int maelys_datalog_input_edb_storage_requirements(size_t, size_t, size_t *, size_t *);
int maelys_datalog_input_edb_init(void *, size_t, size_t, size_t, maelys_datalog_input_edb_t **);
int maelys_datalog_input_edb_create_with_capacity(size_t, size_t, maelys_datalog_input_edb_t **);
int maelys_datalog_input_edb_add_fact(
    maelys_datalog_input_edb_t *, const char *, const maelys_datalog_value_t *,
    size_t, maelys_datalog_diagnostic_t *);
int maelys_datalog_input_edb_add_facts(
    maelys_datalog_input_edb_t *, const maelys_datalog_fact_t *,
    size_t, maelys_datalog_diagnostic_t *);
int maelys_datalog_input_edb_count(const maelys_datalog_input_edb_t *, size_t *);
int maelys_datalog_input_edb_text_usage(const maelys_datalog_input_edb_t *, size_t *, size_t *);
int maelys_datalog_input_edb_view(const maelys_datalog_input_edb_t *,
    const maelys_datalog_fact_t **, size_t *);
int maelys_datalog_input_edb_clear(maelys_datalog_input_edb_t *);
int maelys_datalog_input_edb_free(maelys_datalog_input_edb_t *);
int maelys_datalog_session_solve_edb(
    maelys_datalog_session_t *, const maelys_datalog_input_edb_t *,
    maelys_datalog_result_t **, maelys_datalog_diagnostic_t *);
int maelys_datalog_result_query(
    const maelys_datalog_result_t *result, const char *predicate,
    const maelys_datalog_value_t *terms, size_t arity,
    int *out_present);
int maelys_datalog_result_enumerate(
    const maelys_datalog_result_t *result,
    const char *predicate, size_t arity,
    maelys_datalog_fact_view_t *out_facts,
    size_t out_capacity, size_t *out_count);
int maelys_datalog_result_derived_fact_count(
    const maelys_datalog_result_t *result, size_t *out_count);
int maelys_datalog_result_symbol_text(
    const maelys_datalog_result_t *result, uint32_t symbol_id,
    const char **out_text, size_t *out_length);
int maelys_datalog_result_free(maelys_datalog_result_t *result);
int maelys_datalog_session_explanation_storage_bound(
    const maelys_datalog_session_t *, maelys_datalog_explanation_kind_t,
    size_t *, size_t *);
int maelys_datalog_result_explain_text_in(
    maelys_datalog_result_t *, maelys_datalog_explanation_kind_t,
    const char *, const maelys_datalog_value_t *, size_t,
    void *, size_t, char *, size_t, size_t *);
int maelys_datalog_result_explanation_storage_requirements(
    const maelys_datalog_result_t *, maelys_datalog_explanation_kind_t,
    size_t *, size_t *);
int maelys_datalog_result_prepare_explanation(
    maelys_datalog_result_t *, maelys_datalog_explanation_kind_t,
    const char *, const maelys_datalog_value_t *, size_t,
    void *, size_t, maelys_datalog_prepared_explanation_t **);
int maelys_datalog_prepared_explanation_text_size(
    const maelys_datalog_prepared_explanation_t *, size_t *);
int maelys_datalog_prepared_explanation_write_text(
    const maelys_datalog_prepared_explanation_t *, char *, size_t);
int maelys_datalog_prepared_explanation_release(
    maelys_datalog_prepared_explanation_t *);
int maelys_datalog_result_explain_true_text(
    const maelys_datalog_result_t *, const char *,
    const maelys_datalog_value_t *, size_t, char *, size_t, size_t *);
int maelys_datalog_result_explain_false_text(
    const maelys_datalog_result_t *, const char *,
    const maelys_datalog_value_t *, size_t, char *, size_t, size_t *);
"""
    )
    rpath = "-Wl,-rpath,@loader_path" if platform.system() == "Darwin" else "-Wl,-rpath,$ORIGIN"
    builder.set_source(
        "maelys_datalog._maelys_cffi",
        "#include <maelys/datalog.h>\n"
        '_Static_assert(MAELYS_DATALOG_PUBLIC_API_VERSION == 2u, "Consumer API 2 required");',
        include_dirs=[str(sdk_prefix / "include")],
        library_dirs=[str(PACKAGE)],
        libraries=["maelys_datalog_shared"],
        extra_link_args=[rpath],
    )
    extension_suffix = sysconfig.get_config_var("EXT_SUFFIX") or ".so"
    builder.compile(
        tmpdir=str(Path(__file__).resolve().parent / "build"),
        target=str(PACKAGE / ("_maelys_cffi" + extension_suffix)),
        verbose=True,
    )


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "--sdk-prefix", type=Path, required=True,
        help="Installed SDK prefix containing include/maelys and lib (or lib64)",
    )
    args = parser.parse_args()
    build(args.sdk_prefix.resolve())
