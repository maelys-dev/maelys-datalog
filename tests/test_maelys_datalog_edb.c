#include "src/core/maelys_datalog_edb.h"
#include "src/core/maelys_datalog_domain_registry.h"
#include "examples/domains/maelys_datalog_example_domains.h"
#include "src/core/maelys_datalog_predicate_registry.h"
#include "src/core/maelys_datalog_symbol_table.h"
#include "tests/helpers/test_framework.h"

#include <stdio.h>
#include <string.h>

static maelys_datalog_term_t int_term(long long value) {
    maelys_datalog_term_t t = {.kind = MAELYS_DATALOG_TERM_INT};
    t.as.integer = value;
    return t;
}

static maelys_datalog_term_t symbol_term(maelys_datalog_symbol_id_t value) {
    maelys_datalog_term_t t = {.kind = MAELYS_DATALOG_TERM_SYMBOL};
    t.as.symbol = value;
    return t;
}

static maelys_datalog_term_t bool_term(int value) {
    maelys_datalog_term_t t = {.kind = MAELYS_DATALOG_TERM_BOOL};
    t.as.boolean = value;
    return t;
}

static maelys_datalog_term_t var_term(unsigned value) {
    maelys_datalog_term_t t = {.kind = MAELYS_DATALOG_TERM_VAR};
    t.as.variable = value;
    return t;
}

static maelys_datalog_fact_t fact(maelys_datalog_predicate_id_t predicate_id,
                                  long long value) {
    maelys_datalog_fact_t f;
    memset(&f, 0, sizeof(f));
    f.predicate_id = predicate_id;
    f.arity = 1;
    f.terms[0] = int_term(value);
    return f;
}

static void init_blocked_registry(maelys_datalog_predicate_registry_t *reg) {
    maelys_datalog_predicate_registry_init_core(reg);
    (void)maelys_datalog_example_domains_install();
    (void)maelys_datalog_domain_registry_install("decision", reg);
    (void)maelys_datalog_predicate_registry_add_atom(reg, "proj-1");
    (void)maelys_datalog_predicate_registry_add_atom(reg, "proj-2");
    (void)maelys_datalog_predicate_registry_add_atom(reg, "cli_pivot");
    (void)maelys_datalog_predicate_registry_freeze(reg);
}

static void init_runtime_symbol_registry(maelys_datalog_predicate_registry_t *reg) {
    maelys_datalog_predicate_registry_init_core(reg);
    (void)maelys_datalog_predicate_registry_add_domain(
        reg, "user", 1, MAELYS_DATALOG_PRED_KIND_EDB);
    (void)maelys_datalog_predicate_registry_add_domain(
        reg, "owns", 2, MAELYS_DATALOG_PRED_KIND_EDB);
    (void)maelys_datalog_predicate_registry_add_domain(
        reg, "allow", 1, MAELYS_DATALOG_PRED_KIND_IDB | MAELYS_DATALOG_PRED_KIND_QUERY);
    (void)maelys_datalog_predicate_registry_freeze(reg);
}

static int test_edb_intern_runtime_symbol_basic_idempotent(void) {
    TEST_BEGIN();
    maelys_datalog_predicate_registry_t reg;
    init_runtime_symbol_registry(&reg);
    maelys_datalog_symbol_table_t sym;
    maelys_datalog_symbol_table_init(&sym);
    maelys_datalog_fact_t pool[4];
    maelys_datalog_edb_t edb;
    TEST_ASSERT_EQUAL(MAELYS_OK, maelys_datalog_edb_init(&edb, pool, 4, &sym, &reg), "%d");

    maelys_datalog_symbol_id_t id1 = MAELYS_DATALOG_SYMBOL_ID_INVALID;
    maelys_datalog_symbol_id_t id2 = MAELYS_DATALOG_SYMBOL_ID_INVALID;
    TEST_ASSERT_EQUAL(MAELYS_OK, maelys_datalog_edb_intern_runtime_symbol(&edb, "alice", &id1), "%d");
    TEST_ASSERT_TRUE(id1 != MAELYS_DATALOG_SYMBOL_ID_INVALID);
    TEST_ASSERT_TRUE(maelys_datalog_symbol_id_is_valid(&sym, id1));
    TEST_ASSERT_EQUAL(MAELYS_OK, maelys_datalog_edb_intern_runtime_symbol(&edb, "alice", &id2), "%d");
    TEST_ASSERT_EQUAL(id1, id2, "%u");
    TEST_ASSERT_EQUAL((size_t)1u, sym.count, "%zu");
    TEST_END();
}

static int test_edb_preinterned_symbol_api_rejects_null_args(void) {
    TEST_BEGIN();
    maelys_datalog_predicate_registry_t reg;
    init_runtime_symbol_registry(&reg);
    maelys_datalog_symbol_table_t sym;
    maelys_datalog_symbol_table_init(&sym);
    maelys_datalog_fact_t pool[4];
    maelys_datalog_edb_t edb;
    TEST_ASSERT_EQUAL(MAELYS_OK, maelys_datalog_edb_init(&edb, pool, 4, &sym, &reg), "%d");

    maelys_datalog_symbol_id_t id = 0;
    TEST_ASSERT_EQUAL(MAELYS_ERR_INVALID_ARGUMENT,
                      maelys_datalog_edb_intern_runtime_symbol(NULL, "alice", &id),
                      "%d");
    TEST_ASSERT_EQUAL(MAELYS_ERR_INVALID_ARGUMENT,
                      maelys_datalog_edb_intern_runtime_symbol(&edb, NULL, &id),
                      "%d");
    TEST_ASSERT_EQUAL(MAELYS_ERR_INVALID_ARGUMENT,
                      maelys_datalog_edb_intern_runtime_symbol(&edb, "alice", NULL),
                      "%d");
    TEST_ASSERT_EQUAL(MAELYS_ERR_INVALID_ARGUMENT,
                      maelys_datalog_edb_add_symbol_id_fact(NULL, "user", 1),
                      "%d");
    TEST_ASSERT_EQUAL(MAELYS_ERR_INVALID_ARGUMENT,
                      maelys_datalog_edb_add_symbol_id_fact(&edb, NULL, 1),
                      "%d");
    TEST_ASSERT_EQUAL(MAELYS_ERR_INVALID_ARGUMENT,
                      maelys_datalog_edb_add_symbol_ids_fact(NULL, "owns", 1, 2),
                      "%d");
    TEST_ASSERT_EQUAL(MAELYS_ERR_INVALID_ARGUMENT,
                      maelys_datalog_edb_add_symbol_ids_fact(&edb, NULL, 1, 2),
                      "%d");
    TEST_ASSERT_EQUAL((size_t)0u, edb.fact_count, "%zu");
    TEST_ASSERT_EQUAL((size_t)0u, sym.count, "%zu");
    TEST_END();
}

static int test_edb_add_symbol_id_rejects_invalid_ids(void) {
    TEST_BEGIN();
    maelys_datalog_predicate_registry_t reg;
    init_runtime_symbol_registry(&reg);
    maelys_datalog_symbol_table_t sym;
    maelys_datalog_symbol_table_init(&sym);
    maelys_datalog_fact_t pool[4];
    maelys_datalog_edb_t edb;
    TEST_ASSERT_EQUAL(MAELYS_OK, maelys_datalog_edb_init(&edb, pool, 4, &sym, &reg), "%d");

    maelys_datalog_symbol_id_t alice = MAELYS_DATALOG_SYMBOL_ID_INVALID;
    TEST_ASSERT_EQUAL(MAELYS_OK, maelys_datalog_edb_intern_runtime_symbol(&edb, "alice", &alice), "%d");
    TEST_ASSERT_EQUAL(MAELYS_ERR_INVALID_ARGUMENT,
                      maelys_datalog_edb_add_symbol_id_fact(&edb, "user", MAELYS_DATALOG_SYMBOL_ID_INVALID),
                      "%d");
    TEST_ASSERT_EQUAL(MAELYS_ERR_INVALID_ARGUMENT,
                      maelys_datalog_edb_add_symbol_id_fact(&edb, "user", (maelys_datalog_symbol_id_t)(sym.count + 1u)),
                      "%d");
    TEST_ASSERT_EQUAL((size_t)0u, edb.fact_count, "%zu");
    TEST_ASSERT_EQUAL(MAELYS_OK, maelys_datalog_edb_add_symbol_id_fact(&edb, "user", alice), "%d");
    TEST_ASSERT_EQUAL((size_t)1u, edb.fact_count, "%zu");
    TEST_END();
}

static int test_edb_symbol_ids_validate_schema_before_write(void) {
    TEST_BEGIN();
    maelys_datalog_predicate_registry_t reg;
    init_runtime_symbol_registry(&reg);
    maelys_datalog_symbol_table_t sym;
    maelys_datalog_symbol_table_init(&sym);
    maelys_datalog_fact_t pool[4];
    maelys_datalog_edb_t edb;
    TEST_ASSERT_EQUAL(MAELYS_OK, maelys_datalog_edb_init(&edb, pool, 4, &sym, &reg), "%d");

    maelys_datalog_symbol_id_t alice = 0;
    maelys_datalog_symbol_id_t doc = 0;
    TEST_ASSERT_EQUAL(MAELYS_OK, maelys_datalog_edb_intern_runtime_symbol(&edb, "alice", &alice), "%d");
    TEST_ASSERT_EQUAL(MAELYS_OK, maelys_datalog_edb_intern_runtime_symbol(&edb, "doc.pdf", &doc), "%d");

    TEST_ASSERT_EQUAL(MAELYS_ERR_INVALID_FIELD,
                      maelys_datalog_edb_add_symbol_id_fact(&edb, "missing", alice),
                      "%d");
    TEST_ASSERT_EQUAL((size_t)0u, edb.fact_count, "%zu");
    TEST_ASSERT_EQUAL(MAELYS_ERR_INVALID_FIELD,
                      maelys_datalog_edb_add_symbol_id_fact(&edb, "owns", alice),
                      "%d");
    TEST_ASSERT_EQUAL((size_t)0u, edb.fact_count, "%zu");
    TEST_ASSERT_EQUAL(MAELYS_ERR_INVALID_FIELD,
                      maelys_datalog_edb_add_symbol_ids_fact(&edb, "user", alice, doc),
                      "%d");
    TEST_ASSERT_EQUAL((size_t)0u, edb.fact_count, "%zu");
    TEST_ASSERT_EQUAL(MAELYS_OK, maelys_datalog_edb_add_symbol_ids_fact(&edb, "owns", alice, doc), "%d");
    TEST_ASSERT_EQUAL((size_t)1u, edb.fact_count, "%zu");
    TEST_END();
}

static int test_edb_intern_runtime_symbol_rejects_after_finalize(void) {
    TEST_BEGIN();
    maelys_datalog_predicate_registry_t reg;
    init_runtime_symbol_registry(&reg);
    maelys_datalog_symbol_table_t sym;
    maelys_datalog_symbol_table_init(&sym);
    maelys_datalog_fact_t pool[4];
    maelys_datalog_edb_t edb;
    TEST_ASSERT_EQUAL(MAELYS_OK, maelys_datalog_edb_init(&edb, pool, 4, &sym, &reg), "%d");
    TEST_ASSERT_EQUAL(MAELYS_OK, maelys_datalog_edb_finalize(&edb), "%d");
    maelys_datalog_symbol_id_t id = 0;
    TEST_ASSERT_EQUAL(MAELYS_ERR_INVALID_STATE,
                      maelys_datalog_edb_intern_runtime_symbol(&edb, "alice", &id),
                      "%d");
    TEST_ASSERT_EQUAL(MAELYS_ERR_INVALID_STATE,
                      maelys_datalog_edb_add_symbol_id_fact(&edb, "user", 1),
                      "%d");
    TEST_ASSERT_EQUAL((size_t)0u, edb.fact_count, "%zu");
    TEST_END();
}

static int test_edb_intern_runtime_symbol_capacity_transactional(void) {
    TEST_BEGIN();
    maelys_datalog_predicate_registry_t reg;
    init_runtime_symbol_registry(&reg);
    maelys_datalog_symbol_table_t sym;
    maelys_datalog_symbol_table_init(&sym);
    maelys_datalog_fact_t pool[4];
    maelys_datalog_edb_t edb;
    TEST_ASSERT_EQUAL(MAELYS_OK, maelys_datalog_edb_init(&edb, pool, 4, &sym, &reg), "%d");

    maelys_datalog_symbol_id_t first = 0;
    for (size_t i = 0; i < MAELYS_DATALOG_MAX_SYMBOLS; i++) {
        char text[32];
        snprintf(text, sizeof(text), "sym_%03zu", i);
        maelys_datalog_symbol_id_t id = 0;
        TEST_ASSERT_EQUAL(MAELYS_OK, maelys_datalog_edb_intern_runtime_symbol(&edb, text, &id), "%d");
        if (i == 0) first = id;
    }
    TEST_ASSERT_EQUAL((size_t)MAELYS_DATALOG_MAX_SYMBOLS, sym.count, "%zu");
    size_t before_count = sym.count;
    size_t before_used = sym.used;
    maelys_datalog_symbol_id_t overflow_id = 0;
    TEST_ASSERT_EQUAL(MAELYS_ERR_PAYLOAD_TOO_LARGE,
                      maelys_datalog_edb_intern_runtime_symbol(&edb, "new_symbol_after_full", &overflow_id),
                      "%d");
    TEST_ASSERT_EQUAL(before_count, sym.count, "%zu");
    TEST_ASSERT_EQUAL(before_used, sym.used, "%zu");

    maelys_datalog_symbol_id_t first_again = 0;
    TEST_ASSERT_EQUAL(MAELYS_OK, maelys_datalog_edb_intern_runtime_symbol(&edb, "sym_000", &first_again), "%d");
    TEST_ASSERT_EQUAL(first, first_again, "%u");
    TEST_ASSERT_EQUAL(before_count, sym.count, "%zu");
    TEST_END();
}

static int test_edb_intern_runtime_symbol_deep_copies_input(void) {
    TEST_BEGIN();
    maelys_datalog_predicate_registry_t reg;
    init_runtime_symbol_registry(&reg);
    maelys_datalog_symbol_table_t sym;
    maelys_datalog_symbol_table_init(&sym);
    maelys_datalog_fact_t pool[4];
    maelys_datalog_edb_t edb;
    TEST_ASSERT_EQUAL(MAELYS_OK, maelys_datalog_edb_init(&edb, pool, 4, &sym, &reg), "%d");

    char tmp[32];
    strcpy(tmp, "alice");
    maelys_datalog_symbol_id_t alice = 0;
    TEST_ASSERT_EQUAL(MAELYS_OK, maelys_datalog_edb_intern_runtime_symbol(&edb, tmp, &alice), "%d");
    memset(tmp, 'X', sizeof(tmp));
    TEST_ASSERT_EQUAL_STRING("alice", maelys_datalog_symbol_text(&sym, alice));
    TEST_ASSERT_EQUAL(MAELYS_OK, maelys_datalog_edb_add_symbol_id_fact(&edb, "user", alice), "%d");
    TEST_ASSERT_EQUAL((size_t)1u, edb.fact_count, "%zu");
    TEST_END();
}

static int test_edb_symbol_id_boundaries_and_fresh_state(void) {
    TEST_BEGIN();
    maelys_datalog_predicate_registry_t reg;
    init_runtime_symbol_registry(&reg);
    maelys_datalog_symbol_table_t sym;
    maelys_datalog_symbol_table_init(&sym);
    maelys_datalog_fact_t pool[4];
    maelys_datalog_edb_t edb;
    TEST_ASSERT_EQUAL(MAELYS_OK, maelys_datalog_edb_init(&edb, pool, 4, &sym, &reg), "%d");

    maelys_datalog_symbol_id_t first = 0;
    maelys_datalog_symbol_id_t last = 0;
    TEST_ASSERT_EQUAL(MAELYS_OK, maelys_datalog_edb_intern_runtime_symbol(&edb, "alice", &first), "%d");
    TEST_ASSERT_EQUAL(MAELYS_OK, maelys_datalog_edb_intern_runtime_symbol(&edb, "bob", &last), "%d");
    TEST_ASSERT_EQUAL((maelys_datalog_symbol_id_t)1u, first, "%u");
    TEST_ASSERT_EQUAL((maelys_datalog_symbol_id_t)2u, last, "%u");
    TEST_ASSERT_EQUAL(MAELYS_OK, maelys_datalog_edb_add_symbol_id_fact(&edb, "user", first), "%d");
    TEST_ASSERT_EQUAL(MAELYS_OK, maelys_datalog_edb_add_symbol_id_fact(&edb, "user", last), "%d");
    TEST_ASSERT_EQUAL(MAELYS_ERR_INVALID_ARGUMENT,
                      maelys_datalog_edb_add_symbol_id_fact(&edb, "user", (maelys_datalog_symbol_id_t)(sym.count + 1u)),
                      "%d");

    maelys_datalog_symbol_table_t fresh_sym;
    maelys_datalog_symbol_table_init(&fresh_sym);
    maelys_datalog_fact_t fresh_pool[4];
    maelys_datalog_edb_t fresh_edb;
    TEST_ASSERT_EQUAL(MAELYS_OK, maelys_datalog_edb_init(&fresh_edb, fresh_pool, 4, &fresh_sym, &reg), "%d");
    maelys_datalog_symbol_id_t fresh_id = 0;
    TEST_ASSERT_EQUAL(MAELYS_OK, maelys_datalog_edb_intern_runtime_symbol(&fresh_edb, "alice", &fresh_id), "%d");
    TEST_ASSERT_EQUAL((maelys_datalog_symbol_id_t)1u, fresh_id, "%u");
    TEST_ASSERT_EQUAL((size_t)0u, fresh_edb.fact_count, "%zu");
    TEST_END();
}

static int test_edb_add_query_duplicate(void) {
    TEST_BEGIN();
    maelys_datalog_predicate_registry_t reg;
    init_blocked_registry(&reg);
    maelys_datalog_symbol_table_t sym;
    maelys_datalog_symbol_table_init(&sym);
    maelys_datalog_fact_t pool[4];
    maelys_datalog_edb_t edb;
    TEST_ASSERT_EQUAL(MAELYS_OK, maelys_datalog_edb_init(&edb, pool, 4, &sym, &reg), "%d");
    TEST_ASSERT_EQUAL(MAELYS_OK, maelys_datalog_edb_add_atom_fact(&edb, "blocked", "proj-1"), "%d");
    TEST_ASSERT_EQUAL(MAELYS_OK, maelys_datalog_edb_add_atom_fact(&edb, "blocked", "proj-1"), "%d");
    TEST_ASSERT_EQUAL(MAELYS_OK, maelys_datalog_edb_finalize(&edb), "%d");
    TEST_ASSERT_EQUAL((size_t)1, edb.fact_count, "%zu");
    TEST_ASSERT_TRUE(edb.immutable);
    TEST_ASSERT_TRUE(maelys_datalog_edb_contains(&edb, &pool[0]));
    TEST_END();
}

static int test_edb_overflow_and_unknown_atom(void) {
    TEST_BEGIN();
    maelys_datalog_predicate_registry_t reg;
    init_blocked_registry(&reg);
    maelys_datalog_symbol_table_t sym;
    maelys_datalog_symbol_table_init(&sym);
    maelys_datalog_fact_t pool[1];
    maelys_datalog_edb_t edb;
    TEST_ASSERT_EQUAL(MAELYS_OK, maelys_datalog_edb_init(&edb, pool, 1, &sym, &reg), "%d");
    TEST_ASSERT_EQUAL(MAELYS_ERR_FORBIDDEN, maelys_datalog_edb_add_atom_fact(&edb, "blocked", "unsafe_root_shell"), "%d");
    TEST_ASSERT_EQUAL(MAELYS_OK, maelys_datalog_edb_add_atom_fact(&edb, "blocked", "proj-1"), "%d");
    TEST_ASSERT_EQUAL(MAELYS_ERR_PAYLOAD_TOO_LARGE, maelys_datalog_edb_add_atom_fact(&edb, "blocked", "proj-2"), "%d");
    TEST_END();
}

static int test_datalog_edb_builder_rejects_policy_fact_emission(void) {
    TEST_BEGIN();
    maelys_datalog_predicate_registry_t reg;
    init_blocked_registry(&reg);
    maelys_datalog_symbol_table_t sym;
    maelys_datalog_symbol_table_init(&sym);
    maelys_datalog_fact_t pool[4];
    maelys_datalog_edb_t edb;
    TEST_ASSERT_EQUAL(MAELYS_OK, maelys_datalog_edb_init(&edb, pool, 4, &sym, &reg), "%d");
    TEST_ASSERT_EQUAL((size_t)0u, sym.count, "%zu");
    maelys_datalog_symbol_id_t safe_input = 0;
    TEST_ASSERT_EQUAL(MAELYS_OK, maelys_datalog_symbol_intern(&sym, "cli_pivot", strlen("cli_pivot"),
                                                              &safe_input), "%d");
    maelys_datalog_term_t term = symbol_term(safe_input);
    TEST_ASSERT_EQUAL(MAELYS_ERR_FORBIDDEN,
                      maelys_datalog_edb_add_fact(&edb, "safe", &term, 1),
                      "%d");
    TEST_ASSERT_EQUAL((size_t)0u, edb.fact_count, "%zu");
    TEST_ASSERT_EQUAL((size_t)1u, sym.count, "%zu");
    TEST_END();
}

static int test_datalog_edb_builder_accepts_edb_kind_emission(void) {
    TEST_BEGIN();
    maelys_datalog_predicate_registry_t reg;
    init_blocked_registry(&reg);
    maelys_datalog_symbol_table_t sym;
    maelys_datalog_symbol_table_init(&sym);
    maelys_datalog_fact_t pool[4];
    maelys_datalog_edb_t edb;
    TEST_ASSERT_EQUAL(MAELYS_OK, maelys_datalog_edb_init(&edb, pool, 4, &sym, &reg), "%d");
    maelys_datalog_symbol_id_t sid = 0;
    TEST_ASSERT_EQUAL(MAELYS_OK, maelys_datalog_symbol_intern(&sym, "cli_pivot", strlen("cli_pivot"), &sid), "%d");
    maelys_datalog_term_t term = symbol_term(sid);
    TEST_ASSERT_EQUAL(MAELYS_OK, maelys_datalog_edb_add_fact(&edb, "blocked", &term, 1), "%d");
    TEST_ASSERT_EQUAL((size_t)1u, edb.fact_count, "%zu");
    TEST_END();
}

static int test_datalog_edb_builder_rejects_idb_helper_emission(void) {
    TEST_BEGIN();
    maelys_datalog_predicate_registry_t reg;
    init_blocked_registry(&reg);
    maelys_datalog_symbol_table_t sym;
    maelys_datalog_symbol_table_init(&sym);
    maelys_datalog_fact_t pool[4];
    maelys_datalog_edb_t edb;
    TEST_ASSERT_EQUAL(MAELYS_OK, maelys_datalog_edb_init(&edb, pool, 4, &sym, &reg), "%d");
    maelys_datalog_term_t term = int_term(1);
    TEST_ASSERT_EQUAL(MAELYS_ERR_INVALID_FIELD,
                      maelys_datalog_edb_add_fact(&edb, "allow", &term, 1),
                      "%d");
    TEST_ASSERT_EQUAL(MAELYS_ERR_INVALID_FIELD,
                      maelys_datalog_edb_add_fact(&edb, "reduce", &term, 1),
                      "%d");
    TEST_ASSERT_EQUAL((size_t)0u, edb.fact_count, "%zu");
    TEST_END();
}

static int test_maelys_datalog_fact_sort_canonical_order(void) {
    TEST_BEGIN();
    maelys_datalog_fact_t pool[4] = {
        fact(4, 2),
        fact(2, 9),
        fact(2, 1),
        fact(3, 1),
    };
    maelys_datalog_fact_set_t set;
    maelys_datalog_fact_set_init(&set, pool, 4);
    set.count = 4;
    set.sorted = 0;
    TEST_ASSERT_EQUAL(MAELYS_OK, maelys_datalog_fact_set_sort(&set), "%d");
    TEST_ASSERT_EQUAL((uint16_t)2, set.facts[0].predicate_id, "%u");
    TEST_ASSERT_EQUAL(1LL, set.facts[0].terms[0].as.integer, "%lld");
    TEST_ASSERT_EQUAL((uint16_t)2, set.facts[1].predicate_id, "%u");
    TEST_ASSERT_EQUAL(9LL, set.facts[1].terms[0].as.integer, "%lld");
    TEST_ASSERT_EQUAL((uint16_t)3, set.facts[2].predicate_id, "%u");
    TEST_ASSERT_EQUAL((uint16_t)4, set.facts[3].predicate_id, "%u");
    TEST_END();
}

static int test_maelys_datalog_fact_dedup_removes_duplicates(void) {
    TEST_BEGIN();
    maelys_datalog_fact_t pool[4] = {
        fact(2, 1),
        fact(2, 1),
        fact(2, 2),
        fact(2, 2),
    };
    maelys_datalog_fact_set_t set;
    maelys_datalog_fact_set_init(&set, pool, 4);
    set.count = 4;
    set.sorted = 1;
    TEST_ASSERT_EQUAL(MAELYS_OK, maelys_datalog_fact_set_dedup(&set), "%d");
    TEST_ASSERT_EQUAL((size_t)2, set.count, "%zu");
    TEST_ASSERT_EQUAL(1LL, set.facts[0].terms[0].as.integer, "%lld");
    TEST_ASSERT_EQUAL(2LL, set.facts[1].terms[0].as.integer, "%lld");
    TEST_END();
}

static int test_maelys_datalog_fact_exact_lookup(void) {
    TEST_BEGIN();
    maelys_datalog_fact_t pool[3] = {fact(1, 1), fact(2, 2), fact(3, 3)};
    maelys_datalog_fact_set_t set;
    maelys_datalog_fact_set_init(&set, pool, 3);
    set.count = 3;
    set.sorted = 1;
    TEST_ASSERT_TRUE(maelys_datalog_fact_set_contains(&set, &pool[1]));
    maelys_datalog_fact_t missing = fact(2, 4);
    TEST_ASSERT_FALSE(maelys_datalog_fact_set_contains(&set, &missing));
    TEST_END();
}

static int test_maelys_datalog_fact_predicate_range(void) {
    TEST_BEGIN();
    maelys_datalog_fact_t pool[5] = {fact(1, 1), fact(2, 2), fact(2, 3), fact(2, 4), fact(4, 5)};
    maelys_datalog_fact_set_t set;
    maelys_datalog_fact_set_init(&set, pool, 5);
    set.count = 5;
    set.sorted = 1;
    size_t begin = 0, end = 0;
    TEST_ASSERT_EQUAL(MAELYS_OK, maelys_datalog_fact_set_predicate_range(&set, 2, &begin, &end), "%d");
    TEST_ASSERT_EQUAL((size_t)1, begin, "%zu");
    TEST_ASSERT_EQUAL((size_t)4, end, "%zu");
    TEST_ASSERT_EQUAL(MAELYS_OK, maelys_datalog_fact_set_predicate_range(&set, 3, &begin, &end), "%d");
    TEST_ASSERT_EQUAL(begin, end, "%zu");
    TEST_END();
}

static int test_maelys_datalog_idb_delta_dedup(void) {
    TEST_BEGIN();
    maelys_datalog_fact_t pool[3] = {fact(1, 1), fact(1, 1), fact(1, 2)};
    maelys_datalog_fact_set_t delta;
    maelys_datalog_fact_set_init(&delta, pool, 3);
    delta.count = 3;
    delta.sorted = 0;
    TEST_ASSERT_EQUAL(MAELYS_OK, maelys_datalog_fact_set_sort(&delta), "%d");
    TEST_ASSERT_EQUAL(MAELYS_OK, maelys_datalog_fact_set_dedup(&delta), "%d");
    TEST_ASSERT_EQUAL((size_t)2, delta.count, "%zu");
    TEST_END();
}

static int test_maelys_datalog_idb_merge_delta_preserves_sorted_order(void) {
    TEST_BEGIN();
    maelys_datalog_fact_t current_pool[5] = {fact(1, 1), fact(3, 3)};
    maelys_datalog_fact_t delta_pool[3] = {fact(2, 2), fact(4, 4)};
    maelys_datalog_fact_t merge[5];
    maelys_datalog_fact_set_t current, delta;
    maelys_datalog_fact_set_init(&current, current_pool, 5);
    maelys_datalog_fact_set_init(&delta, delta_pool, 3);
    current.count = 2;
    delta.count = 2;
    size_t inserted = 0;
    TEST_ASSERT_EQUAL(MAELYS_OK, maelys_datalog_fact_set_merge_delta(&current, &delta, merge, 5, &inserted), "%d");
    TEST_ASSERT_EQUAL((size_t)4, current.count, "%zu");
    TEST_ASSERT_EQUAL((size_t)2, inserted, "%zu");
    TEST_ASSERT_EQUAL((uint16_t)1, current.facts[0].predicate_id, "%u");
    TEST_ASSERT_EQUAL((uint16_t)2, current.facts[1].predicate_id, "%u");
    TEST_ASSERT_EQUAL((uint16_t)3, current.facts[2].predicate_id, "%u");
    TEST_ASSERT_EQUAL((uint16_t)4, current.facts[3].predicate_id, "%u");
    TEST_END();
}

static int test_maelys_datalog_idb_merge_delta_reports_inserted_count(void) {
    TEST_BEGIN();
    maelys_datalog_fact_t current_pool[4] = {fact(1, 1), fact(2, 2)};
    maelys_datalog_fact_t delta_pool[3] = {fact(1, 1), fact(3, 3)};
    maelys_datalog_fact_t merge[4];
    maelys_datalog_fact_set_t current, delta;
    maelys_datalog_fact_set_init(&current, current_pool, 4);
    maelys_datalog_fact_set_init(&delta, delta_pool, 3);
    current.count = 2;
    delta.count = 2;
    size_t inserted = 99;
    TEST_ASSERT_EQUAL(MAELYS_OK, maelys_datalog_fact_set_merge_delta(&current, &delta, merge, 4, &inserted), "%d");
    TEST_ASSERT_EQUAL((size_t)1, inserted, "%zu");
    TEST_ASSERT_EQUAL((size_t)3, current.count, "%zu");
    TEST_END();
}

static int test_maelys_datalog_idb_merge_delta_overflow_fails_closed(void) {
    TEST_BEGIN();
    maelys_datalog_fact_t current_pool[3] = {fact(1, 1), fact(2, 2)};
    maelys_datalog_fact_t before[3] = {current_pool[0], current_pool[1], current_pool[2]};
    maelys_datalog_fact_t delta_pool[2] = {fact(3, 3), fact(4, 4)};
    maelys_datalog_fact_t merge[3];
    maelys_datalog_fact_set_t current, delta;
    maelys_datalog_fact_set_init(&current, current_pool, 3);
    maelys_datalog_fact_set_init(&delta, delta_pool, 2);
    current.count = 2;
    delta.count = 2;
    size_t inserted = 0;
    TEST_ASSERT_EQUAL(MAELYS_ERR_PAYLOAD_TOO_LARGE,
                      maelys_datalog_fact_set_merge_delta(&current, &delta, merge, 3, &inserted),
                      "%d");
    TEST_ASSERT_EQUAL((size_t)2, current.count, "%zu");
    TEST_ASSERT_TRUE(maelys_datalog_fact_equals(&current_pool[0], &before[0]));
    TEST_ASSERT_TRUE(maelys_datalog_fact_equals(&current_pool[1], &before[1]));
    TEST_END();
}

static int test_maelys_datalog_idb_merge_delta_one_pass_no_duplicate(void) {
    TEST_BEGIN();
    maelys_datalog_fact_t current_pool[4] = {fact(1, 1)};
    maelys_datalog_fact_t delta_pool[3] = {fact(1, 1), fact(2, 2)};
    maelys_datalog_fact_t merge[4];
    maelys_datalog_fact_set_t current, delta;
    maelys_datalog_fact_set_init(&current, current_pool, 4);
    maelys_datalog_fact_set_init(&delta, delta_pool, 3);
    current.count = 1;
    delta.count = 2;
    size_t inserted = 0;
    TEST_ASSERT_EQUAL(MAELYS_OK, maelys_datalog_fact_set_merge_delta(&current, &delta, merge, 4, &inserted), "%d");
    TEST_ASSERT_EQUAL((size_t)2, current.count, "%zu");
    TEST_ASSERT_EQUAL((size_t)1, inserted, "%zu");
    TEST_ASSERT_TRUE(maelys_datalog_fact_equals(&current.facts[0], &delta_pool[0]));
    TEST_ASSERT_TRUE(maelys_datalog_fact_equals(&current.facts[1], &delta_pool[1]));
    TEST_END();
}

static int test_maelys_datalog_term_equal_int(void) {
    TEST_BEGIN();
    maelys_datalog_term_t a = int_term(42);
    maelys_datalog_term_t b = int_term(42);
    maelys_datalog_term_t c = int_term(43);
    TEST_ASSERT_TRUE(maelys_datalog_term_equal(&a, &b));
    TEST_ASSERT_FALSE(maelys_datalog_term_equal(&a, &c));
    TEST_ASSERT_FALSE(maelys_datalog_term_equal(&a, NULL));
    TEST_END();
}

static int test_maelys_datalog_term_equal_symbol(void) {
    TEST_BEGIN();
    maelys_datalog_term_t a = symbol_term(7);
    maelys_datalog_term_t b = symbol_term(7);
    maelys_datalog_term_t c = symbol_term(8);
    TEST_ASSERT_TRUE(maelys_datalog_term_equal(&a, &b));
    TEST_ASSERT_FALSE(maelys_datalog_term_equal(&a, &c));
    TEST_END();
}

static int test_maelys_datalog_term_equal_bool(void) {
    TEST_BEGIN();
    maelys_datalog_term_t a = bool_term(1);
    maelys_datalog_term_t b = bool_term(1);
    maelys_datalog_term_t c = bool_term(0);
    TEST_ASSERT_TRUE(maelys_datalog_term_equal(&a, &b));
    TEST_ASSERT_FALSE(maelys_datalog_term_equal(&a, &c));
    TEST_END();
}

static int test_maelys_datalog_term_equal_var_false(void) {
    TEST_BEGIN();
    maelys_datalog_term_t a = var_term(1);
    maelys_datalog_term_t b = var_term(1);
    TEST_ASSERT_FALSE(maelys_datalog_term_equal(&a, &b));
    TEST_END();
}

static int test_maelys_datalog_term_equal_kind_mismatch(void) {
    TEST_BEGIN();
    maelys_datalog_term_t a = int_term(1);
    maelys_datalog_term_t b = bool_term(1);
    TEST_ASSERT_FALSE(maelys_datalog_term_equal(&a, &b));
    TEST_END();
}

static int test_maelys_datalog_term_equal_padding_sensitive(void) {
    TEST_BEGIN();
    maelys_datalog_term_t a;
    maelys_datalog_term_t b;
    memset(&a, 0xAA, sizeof(a));
    memset(&b, 0x55, sizeof(b));
    a.kind = MAELYS_DATALOG_TERM_INT;
    a.as.integer = 42;
    b.kind = MAELYS_DATALOG_TERM_INT;
    b.as.integer = 42;
    TEST_ASSERT_TRUE(maelys_datalog_term_equal(&a, &b));
    b.as.integer = 43;
    TEST_ASSERT_FALSE(maelys_datalog_term_equal(&a, &b));
    TEST_END();
}

int main(int argc, char **argv) {
    test_case_t cases[] = {
        {"maelys_datalog_edb/add_query_duplicate", TEST_MODE_NON_BLOCKING, test_edb_add_query_duplicate},
        {"maelys_datalog_edb/overflow_unknown_atom", TEST_MODE_NON_BLOCKING, test_edb_overflow_and_unknown_atom},
        {"maelys_datalog_edb/intern_runtime_symbol_basic_idempotent", TEST_MODE_NON_BLOCKING, test_edb_intern_runtime_symbol_basic_idempotent},
        {"maelys_datalog_edb/preinterned_symbol_api_rejects_null_args", TEST_MODE_NON_BLOCKING, test_edb_preinterned_symbol_api_rejects_null_args},
        {"maelys_datalog_edb/add_symbol_id_rejects_invalid_ids", TEST_MODE_NON_BLOCKING, test_edb_add_symbol_id_rejects_invalid_ids},
        {"maelys_datalog_edb/symbol_ids_validate_schema_before_write", TEST_MODE_NON_BLOCKING, test_edb_symbol_ids_validate_schema_before_write},
        {"maelys_datalog_edb/intern_runtime_symbol_rejects_after_finalize", TEST_MODE_NON_BLOCKING, test_edb_intern_runtime_symbol_rejects_after_finalize},
        {"maelys_datalog_edb/intern_runtime_symbol_capacity_transactional", TEST_MODE_NON_BLOCKING, test_edb_intern_runtime_symbol_capacity_transactional},
        {"maelys_datalog_edb/intern_runtime_symbol_deep_copies_input", TEST_MODE_NON_BLOCKING, test_edb_intern_runtime_symbol_deep_copies_input},
        {"maelys_datalog_edb/symbol_id_boundaries_and_fresh_state", TEST_MODE_NON_BLOCKING, test_edb_symbol_id_boundaries_and_fresh_state},
        {"maelys_datalog_edb/policy_fact_emission_forbidden", TEST_MODE_NON_BLOCKING, test_datalog_edb_builder_rejects_policy_fact_emission},
        {"maelys_datalog_edb/edb_kind_emission_accepted", TEST_MODE_NON_BLOCKING, test_datalog_edb_builder_accepts_edb_kind_emission},
        {"maelys_datalog_edb/idb_helper_emission_rejected", TEST_MODE_NON_BLOCKING, test_datalog_edb_builder_rejects_idb_helper_emission},
        {"maelys_datalog_edb/fact_sort_canonical_order", TEST_MODE_NON_BLOCKING, test_maelys_datalog_fact_sort_canonical_order},
        {"maelys_datalog_edb/fact_dedup_removes_duplicates", TEST_MODE_NON_BLOCKING, test_maelys_datalog_fact_dedup_removes_duplicates},
        {"maelys_datalog_edb/fact_exact_lookup", TEST_MODE_NON_BLOCKING, test_maelys_datalog_fact_exact_lookup},
        {"maelys_datalog_edb/fact_predicate_range", TEST_MODE_NON_BLOCKING, test_maelys_datalog_fact_predicate_range},
        {"maelys_datalog_edb/idb_delta_dedup", TEST_MODE_NON_BLOCKING, test_maelys_datalog_idb_delta_dedup},
        {"maelys_datalog_edb/idb_merge_delta_preserves_sorted_order", TEST_MODE_NON_BLOCKING, test_maelys_datalog_idb_merge_delta_preserves_sorted_order},
        {"maelys_datalog_edb/idb_merge_delta_reports_inserted_count", TEST_MODE_NON_BLOCKING, test_maelys_datalog_idb_merge_delta_reports_inserted_count},
        {"maelys_datalog_edb/idb_merge_delta_overflow_fails_closed", TEST_MODE_NON_BLOCKING, test_maelys_datalog_idb_merge_delta_overflow_fails_closed},
        {"maelys_datalog_edb/idb_merge_delta_one_pass_no_duplicate", TEST_MODE_NON_BLOCKING, test_maelys_datalog_idb_merge_delta_one_pass_no_duplicate},
        {"maelys_datalog_edb/term_equal_int", TEST_MODE_NON_BLOCKING, test_maelys_datalog_term_equal_int},
        {"maelys_datalog_edb/term_equal_symbol", TEST_MODE_NON_BLOCKING, test_maelys_datalog_term_equal_symbol},
        {"maelys_datalog_edb/term_equal_bool", TEST_MODE_NON_BLOCKING, test_maelys_datalog_term_equal_bool},
        {"maelys_datalog_edb/term_equal_var_false", TEST_MODE_NON_BLOCKING, test_maelys_datalog_term_equal_var_false},
        {"maelys_datalog_edb/term_equal_kind_mismatch", TEST_MODE_NON_BLOCKING, test_maelys_datalog_term_equal_kind_mismatch},
        {"maelys_datalog_edb/term_equal_padding_sensitive", TEST_MODE_NON_BLOCKING, test_maelys_datalog_term_equal_padding_sensitive},
    };
    return test_main("maelys_datalog_edb", cases, (int)(sizeof(cases) / sizeof(cases[0])), argc, argv);
}
