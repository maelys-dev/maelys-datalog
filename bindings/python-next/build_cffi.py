"""Build the experimental binding directly against the opaque public facade."""

from __future__ import annotations

import argparse
import platform
import shutil
import sysconfig
import tempfile
from pathlib import Path

from cffi import FFI


ROOT = Path(__file__).resolve().parents[2]
PACKAGE = Path(__file__).resolve().parent / "maelys_datalog_next"


def build(native_build_dir: Path, engine_dir: Path = ROOT) -> None:
    header = engine_dir / "include/maelys/datalog.h"
    if not header.is_file():
        raise SystemExit(f"Missing public facade header: {header}")
    suffix = ".dylib" if platform.system() == "Darwin" else ".so"
    library_name = "libmaelys_datalog_shared" + suffix
    library = native_build_dir / library_name
    if not library.is_file():
        raise SystemExit(
            f"Missing {library}; build the maelys_datalog_shared CMake target first"
        )
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
#define MAELYS_DATALOG_VALUE_SYMBOL ...
#define MAELYS_DATALOG_VALUE_INTEGER ...
#define MAELYS_DATALOG_VALUE_BOOLEAN ...
#define MAELYS_DATALOG_PREDICATE_EDB ...
#define MAELYS_DATALOG_PREDICATE_IDB ...
#define MAELYS_DATALOG_PREDICATE_QUERY ...
#define MAELYS_DATALOG_PREDICATE_POLICY_FACT ...
#define MAELYS_DATALOG_STATUS_OK ...
#define MAELYS_DATALOG_STATUS_NOT_FOUND ...
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
#define MAELYS_DATALOG_STATUS_PAYLOAD_TOO_LARGE ...
int maelys_datalog_session_config_create(maelys_datalog_session_config_t **);
int maelys_datalog_session_config_set_required_capabilities(maelys_datalog_session_config_t *, uint64_t);
int maelys_datalog_session_config_get_required_capabilities(const maelys_datalog_session_config_t *, uint64_t *);
int maelys_datalog_session_config_set_work_limit(maelys_datalog_session_config_t *, uint64_t);
int maelys_datalog_session_config_get_work_limit(const maelys_datalog_session_config_t *, uint64_t *);
int maelys_datalog_session_config_free(maelys_datalog_session_config_t *);
int maelys_datalog_session_create_configured(
    const maelys_datalog_policy_t *, size_t,
    const maelys_datalog_session_config_t *, maelys_datalog_session_t **);
int maelys_datalog_session_execution_fingerprint(const maelys_datalog_session_t *, char[65]);
typedef struct {
    int source;
    int code;
    size_t line;
    size_t column;
    char phase[32];
    char message[256];
    char hint[256];
} maelys_datalog_public_diagnostic_t;
typedef struct {
    const char *name;
    size_t arity;
    unsigned flags;
} maelys_datalog_public_predicate_t;
typedef struct {
    const char *name;
    const maelys_datalog_public_predicate_t *predicates;
    size_t predicate_count;
    const char *const *atoms;
    size_t atom_count;
} maelys_datalog_public_domain_t;
typedef struct {
    int kind;
    union {
        const char *symbol;
        int64_t integer;
        int boolean;
    } as;
} maelys_datalog_public_value_t;
typedef struct {
    const char *predicate;
    size_t arity;
    maelys_datalog_public_value_t terms[4];
} maelys_datalog_public_fact_t;
typedef struct {
    int kind;
    union {
        uint32_t symbol_id;
        int64_t integer;
        int boolean;
    } as;
} maelys_datalog_public_term_view_t;
typedef struct {
    size_t arity;
    maelys_datalog_public_term_view_t terms[4];
} maelys_datalog_public_fact_view_t;

const char *maelys_datalog_status_name(int status);
int maelys_datalog_limit_get(int limit, size_t *out_value);
void maelys_datalog_public_diagnostic_clear(
    maelys_datalog_public_diagnostic_t *diagnostic);
int maelys_datalog_domain_register(
    const maelys_datalog_public_domain_t *domain);
int maelys_datalog_policy_load_inline(
    const char *domain, const char *policy_id,
    const char *source, size_t source_length,
    maelys_datalog_policy_t **out_policy,
    maelys_datalog_public_diagnostic_t *out_diagnostic);
int maelys_datalog_policy_free(maelys_datalog_policy_t *policy);
int maelys_datalog_policy_load_manifest(
    const char *, unsigned, maelys_datalog_policy_t **,
    maelys_datalog_public_diagnostic_t *);
int maelys_datalog_policy_count(const maelys_datalog_policy_t *, size_t *);
int maelys_datalog_policy_fingerprint(const maelys_datalog_policy_t *, char[65]);
int maelys_datalog_session_fingerprint(const maelys_datalog_session_t *, char[65]);
int maelys_datalog_session_create(
    const maelys_datalog_policy_t *policy, size_t policy_index,
    maelys_datalog_session_t **out_session);
int maelys_datalog_session_solve(
    maelys_datalog_session_t *session,
    const maelys_datalog_public_fact_t *facts, size_t fact_count,
    maelys_datalog_result_t **out_result,
    maelys_datalog_public_diagnostic_t *out_diagnostic);
int maelys_datalog_session_free(maelys_datalog_session_t *session);
int maelys_datalog_input_edb_create(maelys_datalog_input_edb_t **);
int maelys_datalog_input_edb_storage_requirements(size_t, size_t, size_t *, size_t *);
int maelys_datalog_input_edb_init(void *, size_t, size_t, size_t, maelys_datalog_input_edb_t **);
int maelys_datalog_input_edb_create_with_capacity(size_t, size_t, maelys_datalog_input_edb_t **);
int maelys_datalog_input_edb_add_fact(
    maelys_datalog_input_edb_t *, const char *, const maelys_datalog_public_value_t *,
    size_t, maelys_datalog_public_diagnostic_t *);
int maelys_datalog_input_edb_add_facts(
    maelys_datalog_input_edb_t *, const maelys_datalog_public_fact_t *,
    size_t, maelys_datalog_public_diagnostic_t *);
int maelys_datalog_input_edb_count(const maelys_datalog_input_edb_t *, size_t *);
int maelys_datalog_input_edb_clear(maelys_datalog_input_edb_t *);
int maelys_datalog_input_edb_free(maelys_datalog_input_edb_t *);
int maelys_datalog_session_solve_edb(
    maelys_datalog_session_t *, const maelys_datalog_input_edb_t *,
    maelys_datalog_result_t **, maelys_datalog_public_diagnostic_t *);
int maelys_datalog_result_query(
    const maelys_datalog_result_t *result, const char *predicate,
    const maelys_datalog_public_value_t *terms, size_t arity,
    int *out_present);
int maelys_datalog_result_enumerate(
    const maelys_datalog_result_t *result,
    const char *predicate, size_t arity,
    maelys_datalog_public_fact_view_t *out_facts,
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
    const char *, const maelys_datalog_public_value_t *, size_t,
    void *, size_t, char *, size_t, size_t *);
int maelys_datalog_result_explanation_storage_requirements(
    const maelys_datalog_result_t *, maelys_datalog_explanation_kind_t,
    size_t *, size_t *);
int maelys_datalog_result_prepare_explanation(
    maelys_datalog_result_t *, maelys_datalog_explanation_kind_t,
    const char *, const maelys_datalog_public_value_t *, size_t,
    void *, size_t, maelys_datalog_prepared_explanation_t **);
int maelys_datalog_prepared_explanation_text_size(
    const maelys_datalog_prepared_explanation_t *, size_t *);
int maelys_datalog_prepared_explanation_write_text(
    const maelys_datalog_prepared_explanation_t *, char *, size_t);
int maelys_datalog_prepared_explanation_release(
    maelys_datalog_prepared_explanation_t *);
int maelys_datalog_result_explain_true_text(
    const maelys_datalog_result_t *, const char *,
    const maelys_datalog_public_value_t *, size_t, char *, size_t, size_t *);
int maelys_datalog_result_explain_false_text(
    const maelys_datalog_result_t *, const char *,
    const maelys_datalog_public_value_t *, size_t, char *, size_t, size_t *);
"""
    )
    rpath = "-Wl,-rpath,@loader_path" if platform.system() == "Darwin" else "-Wl,-rpath,$ORIGIN"
    builder.set_source(
        "maelys_datalog_next._maelys_cffi",
        "#include <maelys/datalog.h>",
        include_dirs=[str(engine_dir / "include")],
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
        "--engine-dir",
        type=Path,
        default=ROOT,
        help="Development only: engine source checkout providing the header matching --build-dir",
    )
    parser.add_argument(
        "--build-dir",
        default="build/python-next",
        help="CMake build directory containing libmaelys_datalog_shared",
    )
    args = parser.parse_args()
    selected = Path(args.build_dir)
    engine_dir = args.engine_dir.resolve()
    build((selected if selected.is_absolute() else engine_dir / selected).resolve(), engine_dir)
