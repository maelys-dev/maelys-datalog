/* P4-C65 — Bounded, deterministic Why-true text formatter tests.
 *
 * Golden strings (§6.1), buffer/atomicity (§6.2), defensive validation
 * (§6.3), non-mutation and determinism (§6.4).
 *
 * Per the directive, every maelys_datalog_explanation_t and every large text
 * buffer is allocated OFF the stack (file-static). */

#include "src/core/maelys_datalog_edb.h"
#include "src/core/maelys_datalog_explanation_format.h"
#include "src/core/maelys_datalog_parser.h"
#include "src/core/maelys_datalog_predicate_registry.h"
#include "src/core/maelys_datalog_ruleset.h"
#include "src/core/maelys_datalog_solver.h"
#include "src/core/maelys_datalog_symbol_table.h"
#include "tests/helpers/test_framework.h"

#include <limits.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

/* MAELYS_TESTING hook: reference recursive traversal (§6.4 double parcours). */
maelys_result_t maelys_datalog_test_solve_once_legacy_order(
    const maelys_datalog_ruleset_t *ruleset,
    const maelys_datalog_edb_t *edb,
    maelys_datalog_solve_result_t **out_result,
    maelys_datalog_solve_diagnostic_t *out_diag);

/* ====================================================================
 * §6.2(6) — static SIZE_MAX proof: with the public build bounds, the largest
 * renderable text is astronomically below SIZE_MAX, so the writer's SIZE_MAX
 * guard is unreachable through any publicly constructible input. Upper
 * bounds: a quoted symbol costs at most 4 bytes per input byte plus quotes;
 * a fact at most a quoted 63-byte name plus MAX_ARITY quoted symbols; a line
 * at most two facts plus fixed tokens and decimal fields.
 * ==================================================================== */
#define FMT_T_MAX_SYM (2u + 4u * (size_t)MAELYS_DATALOG_MAX_STRING_BYTES)
#define FMT_T_MAX_FACT (2u + 4u * 64u + 2u + \
                        (size_t)MAELYS_DATALOG_MAX_ARITY * (FMT_T_MAX_SYM + 1u))
#define FMT_T_MAX_LINE (128u + 2u * FMT_T_MAX_FACT)
#define FMT_T_MAX_TEXT \
    (FMT_T_MAX_LINE * (4u + (size_t)MAELYS_DATALOG_MAX_EXPLANATION_STEPS + \
                       (size_t)MAELYS_DATALOG_MAX_EXPLANATION_PREMISES))
_Static_assert(FMT_T_MAX_TEXT < (SIZE_MAX / 1024u),
               "bounded explanation text cannot approach SIZE_MAX");

static const char k_zero_sha[] =
    "0000000000000000000000000000000000000000000000000000000000000000";

/* ---- file-static (off-stack) fixtures and buffers ---- */
static maelys_datalog_ruleset_t g_fx;       /* main vocabulary fixture */
static maelys_datalog_ruleset_t g_fx_copy;  /* bit copy for non-mutation */
static maelys_datalog_ruleset_t g_bad_rs;   /* corrupted ruleset copies */
static maelys_datalog_ruleset_t g_ord_r1;   /* ordered-traversal fixture */
static maelys_datalog_ruleset_t g_ord_r2;   /* reference-traversal fixture */
static maelys_datalog_explanation_t g_exp;
static maelys_datalog_explanation_t g_exp_b;
static maelys_datalog_explanation_t g_exp_copy;
static maelys_datalog_explanation_t g_bad;
static char g_text_a[65536];
static char g_text_b[65536];
static char g_expected[65536];

static int g_fx_ready = 0;
static maelys_datalog_predicate_id_t g_pid_owns;
static maelys_datalog_predicate_id_t g_pid_blocked;
static maelys_datalog_predicate_id_t g_pid_allow;
static maelys_datalog_predicate_id_t g_pid_pol;
static maelys_datalog_predicate_id_t g_pid_num;
static maelys_datalog_predicate_id_t g_pid_flag;
static maelys_datalog_predicate_id_t g_pid_zero;
static maelys_datalog_predicate_id_t g_pid_str;
static maelys_datalog_predicate_id_t g_pid_maxname;
static maelys_datalog_symbol_id_t g_sym_alice;
static maelys_datalog_symbol_id_t g_sym_doc;
static maelys_datalog_symbol_id_t g_sym_cafe;
static maelys_datalog_symbol_id_t g_sym_lf;
static maelys_datalog_symbol_id_t g_sym_qslash;
static maelys_datalog_symbol_id_t g_sym_tabcr;
static maelys_datalog_symbol_id_t g_sym_nul;
static maelys_datalog_symbol_id_t g_sym_ctl;
static maelys_datalog_symbol_id_t g_sym_bad;
static maelys_datalog_symbol_id_t g_sym_badmix;
static maelys_datalog_symbol_id_t g_sym_max;
static char g_maxname[64];
static char g_maxsym[MAELYS_DATALOG_MAX_STRING_BYTES];

static maelys_datalog_symbol_id_t fx_intern(const char *bytes, size_t len) {
    maelys_datalog_symbol_id_t id = 0;
    if (maelys_datalog_symbol_intern(&g_fx.symbols, bytes, len, &id) != MAELYS_OK) {
        return 0;
    }
    return id;
}

static int fx_find(const char *name, size_t arity, maelys_datalog_predicate_id_t *out) {
    return maelys_datalog_predicate_registry_find(&g_fx.registry, name, arity, out);
}

static int ensure_fx(void) {
    if (g_fx_ready) return 1;
    memset(&g_fx, 0, sizeof(g_fx));
    if (maelys_datalog_ruleset_init(&g_fx, "p4c65.format.fixture", "format",
                                    k_zero_sha, 1) != MAELYS_OK) {
        return 0;
    }
    memset(g_maxname, 'p', 63u);
    g_maxname[63] = '\0';
    static const struct {
        const char *name;
        size_t arity;
        unsigned kind;
    } defs[] = {
        {"owns", 2u, MAELYS_DATALOG_PRED_KIND_EDB},
        {"blocked", 1u, MAELYS_DATALOG_PRED_KIND_EDB},
        {"pol", 1u, MAELYS_DATALOG_PRED_KIND_POLICY_FACT},
        {"num", 1u, MAELYS_DATALOG_PRED_KIND_EDB},
        {"flag", 1u, MAELYS_DATALOG_PRED_KIND_EDB},
        {"zero", 0u, MAELYS_DATALOG_PRED_KIND_EDB},
        {"str", 1u, MAELYS_DATALOG_PRED_KIND_EDB},
        {"allow", 2u,
         MAELYS_DATALOG_PRED_KIND_IDB | MAELYS_DATALOG_PRED_KIND_QUERY},
        {"fill1", 2u, MAELYS_DATALOG_PRED_KIND_IDB | MAELYS_DATALOG_PRED_KIND_QUERY},
        {"fill2", 2u, MAELYS_DATALOG_PRED_KIND_IDB | MAELYS_DATALOG_PRED_KIND_QUERY},
        {"fill3", 2u, MAELYS_DATALOG_PRED_KIND_IDB | MAELYS_DATALOG_PRED_KIND_QUERY},
        {"fill4", 2u, MAELYS_DATALOG_PRED_KIND_IDB | MAELYS_DATALOG_PRED_KIND_QUERY},
        {"fill5", 2u, MAELYS_DATALOG_PRED_KIND_IDB | MAELYS_DATALOG_PRED_KIND_QUERY},
        {"fill6", 2u, MAELYS_DATALOG_PRED_KIND_IDB | MAELYS_DATALOG_PRED_KIND_QUERY},
    };
    for (size_t i = 0; i < sizeof(defs) / sizeof(defs[0]); i++) {
        if (maelys_datalog_predicate_registry_add_domain(&g_fx.registry,
                                                         defs[i].name,
                                                         defs[i].arity,
                                                         defs[i].kind) != MAELYS_OK) {
            return 0;
        }
    }
    if (maelys_datalog_predicate_registry_add_domain(
            &g_fx.registry, g_maxname, 1u, MAELYS_DATALOG_PRED_KIND_EDB) != MAELYS_OK) {
        return 0;
    }
    if (maelys_datalog_predicate_registry_freeze(&g_fx.registry) != MAELYS_OK) {
        return 0;
    }
    static const char src[] =
        "fill1(X, Y) :- owns(X, Y).\n"
        "fill2(X, Y) :- owns(X, Y).\n"
        "fill3(X, Y) :- owns(X, Y).\n"
        "fill4(X, Y) :- owns(X, Y).\n"
        "fill5(X, Y) :- owns(X, Y).\n"
        "fill6(X, Y) :- owns(X, Y).\n"
        "allow(U, D) :- owns(U, D), not(blocked(U)).\n";
    if (maelys_datalog_parse_ruleset(&g_fx, src, sizeof(src) - 1u) != MAELYS_OK) {
        return 0;
    }
    if (g_fx.rule_count != 7u) return 0;
    if (!fx_find("owns", 2u, &g_pid_owns)) return 0;
    if (!fx_find("blocked", 1u, &g_pid_blocked)) return 0;
    if (!fx_find("allow", 2u, &g_pid_allow)) return 0;
    if (!fx_find("pol", 1u, &g_pid_pol)) return 0;
    if (!fx_find("num", 1u, &g_pid_num)) return 0;
    if (!fx_find("flag", 1u, &g_pid_flag)) return 0;
    if (!fx_find("zero", 0u, &g_pid_zero)) return 0;
    if (!fx_find("str", 1u, &g_pid_str)) return 0;
    if (!fx_find(g_maxname, 1u, &g_pid_maxname)) return 0;

    memset(g_maxsym, 'a', sizeof(g_maxsym));
    g_sym_alice = fx_intern("alice", 5u);
    g_sym_doc = fx_intern("doc.pdf", 7u);
    g_sym_cafe = fx_intern("caf\xC3\xA9", 5u);
    g_sym_lf = fx_intern("line\nbreak", 10u);
    g_sym_qslash = fx_intern("quote\"slash\\", 12u);
    g_sym_tabcr = fx_intern("tab\tcr\rx", 8u);
    g_sym_nul = fx_intern("nul\0inside", 10u);
    g_sym_ctl = fx_intern("bell\x07" "del\x7F", 9u);
    g_sym_bad = fx_intern("\xFF" "bad", 4u);
    g_sym_badmix = fx_intern("x\xFF\n\r\t", 5u);
    g_sym_max = fx_intern(g_maxsym, sizeof(g_maxsym));
    if (!g_sym_alice || !g_sym_doc || !g_sym_cafe || !g_sym_lf || !g_sym_qslash ||
        !g_sym_tabcr || !g_sym_nul || !g_sym_ctl || !g_sym_bad || !g_sym_badmix ||
        !g_sym_max) {
        return 0;
    }
    g_fx_ready = 1;
    return 1;
}

/* ---- explanation construction helpers ---- */

static maelys_datalog_term_t t_sym(maelys_datalog_symbol_id_t id) {
    maelys_datalog_term_t t;
    memset(&t, 0, sizeof(t));
    t.kind = MAELYS_DATALOG_TERM_SYMBOL;
    t.as.symbol = id;
    return t;
}

static maelys_datalog_term_t t_int(long long v) {
    maelys_datalog_term_t t;
    memset(&t, 0, sizeof(t));
    t.kind = MAELYS_DATALOG_TERM_INT;
    t.as.integer = v;
    return t;
}

static maelys_datalog_term_t t_bool(int b) {
    maelys_datalog_term_t t;
    memset(&t, 0, sizeof(t));
    t.kind = MAELYS_DATALOG_TERM_BOOL;
    t.as.boolean = b ? 1 : 0;
    return t;
}

static maelys_datalog_fact_t f0(maelys_datalog_predicate_id_t pid) {
    maelys_datalog_fact_t f;
    memset(&f, 0, sizeof(f));
    f.predicate_id = pid;
    f.arity = 0u;
    return f;
}

static maelys_datalog_fact_t f1(maelys_datalog_predicate_id_t pid,
                                maelys_datalog_term_t a) {
    maelys_datalog_fact_t f;
    memset(&f, 0, sizeof(f));
    f.predicate_id = pid;
    f.arity = 1u;
    f.terms[0] = a;
    return f;
}

static maelys_datalog_fact_t f2(maelys_datalog_predicate_id_t pid,
                                maelys_datalog_term_t a,
                                maelys_datalog_term_t b) {
    maelys_datalog_fact_t f;
    memset(&f, 0, sizeof(f));
    f.predicate_id = pid;
    f.arity = 2u;
    f.terms[0] = a;
    f.terms[1] = b;
    return f;
}

static void set_step(maelys_datalog_explanation_t *e,
                     uint16_t idx,
                     size_t rule_id,
                     maelys_datalog_fact_t fact,
                     uint16_t begin,
                     uint16_t count) {
    memset(&e->steps[idx], 0, sizeof(e->steps[idx]));
    e->steps[idx].rule_id = rule_id;
    e->steps[idx].derived_fact = fact;
    e->steps[idx].premise_begin = begin;
    e->steps[idx].premise_count = count;
}

static void set_pos(maelys_datalog_explanation_t *e,
                    uint16_t idx,
                    uint16_t body,
                    uint8_t origin,
                    maelys_datalog_fact_t fact,
                    uint16_t parent) {
    memset(&e->premises[idx], 0, sizeof(e->premises[idx]));
    e->premises[idx].kind = (uint8_t)MAELYS_DATALOG_EXPLANATION_PREMISE_POSITIVE_FACT;
    e->premises[idx].origin = origin;
    e->premises[idx].body_index = body;
    e->premises[idx].parent_step = parent;
    e->premises[idx].as.fact = fact;
}

static void set_neg(maelys_datalog_explanation_t *e,
                    uint16_t idx,
                    uint16_t body,
                    uint8_t origin,
                    maelys_datalog_fact_t fact) {
    memset(&e->premises[idx], 0, sizeof(e->premises[idx]));
    e->premises[idx].kind = (uint8_t)MAELYS_DATALOG_EXPLANATION_PREMISE_NEGATED_ABSENCE;
    e->premises[idx].origin = origin;
    e->premises[idx].body_index = body;
    e->premises[idx].parent_step = (uint16_t)MAELYS_DATALOG_EXPLANATION_NO_STEP;
    e->premises[idx].as.fact = fact;
}

static void set_cmp(maelys_datalog_explanation_t *e,
                    uint16_t idx,
                    uint16_t body,
                    uint8_t op,
                    maelys_datalog_term_t lhs,
                    maelys_datalog_term_t rhs) {
    memset(&e->premises[idx], 0, sizeof(e->premises[idx]));
    e->premises[idx].kind = (uint8_t)MAELYS_DATALOG_EXPLANATION_PREMISE_COMPARISON_TRUE;
    e->premises[idx].origin = (uint8_t)MAELYS_DATALOG_EXPLANATION_ORIGIN_NOT_APPLICABLE;
    e->premises[idx].body_index = body;
    e->premises[idx].parent_step = (uint16_t)MAELYS_DATALOG_EXPLANATION_NO_STEP;
    e->premises[idx].op = op;
    e->premises[idx].as.comparison.lhs = lhs;
    e->premises[idx].as.comparison.rhs = rhs;
}

static void finish(maelys_datalog_explanation_t *e,
                   uint16_t step_count,
                   uint16_t premise_count) {
    e->found = 1u;
    e->truncated = 0u;
    e->step_count = step_count;
    e->premise_count = premise_count;
}

#define ORIGIN_POLICY ((uint8_t)MAELYS_DATALOG_EXPLANATION_ORIGIN_POLICY_FACT)
#define ORIGIN_EDB ((uint8_t)MAELYS_DATALOG_EXPLANATION_ORIGIN_EDB)
#define ORIGIN_IDB ((uint8_t)MAELYS_DATALOG_EXPLANATION_ORIGIN_IDB)
#define ORIGIN_NA ((uint8_t)MAELYS_DATALOG_EXPLANATION_ORIGIN_NOT_APPLICABLE)
#define NO_STEP ((uint16_t)MAELYS_DATALOG_EXPLANATION_NO_STEP)

/* §3.3 normative example: one EDB premise + one negated absence, rule 7. */
static void build_normative_example(maelys_datalog_explanation_t *e) {
    memset(e, 0, sizeof(*e));
    set_step(e, 0u, 7u, f2(g_pid_allow, t_sym(g_sym_alice), t_sym(g_sym_doc)), 0u, 2u);
    set_pos(e, 0u, 0u, ORIGIN_EDB,
            f2(g_pid_owns, t_sym(g_sym_alice), t_sym(g_sym_doc)), NO_STEP);
    set_neg(e, 1u, 1u, ORIGIN_EDB, f1(g_pid_blocked, t_sym(g_sym_alice)));
    finish(e, 1u, 2u);
}

/* Three steps, two distinct IDB parents referenced by the result step. */
static void build_parent_chain(maelys_datalog_explanation_t *e) {
    memset(e, 0, sizeof(*e));
    set_step(e, 0u, 1u, f1(g_pid_num, t_int(1)), 0u, 1u);
    set_pos(e, 0u, 0u, ORIGIN_EDB, f1(g_pid_num, t_int(10)), NO_STEP);
    set_step(e, 1u, 2u, f1(g_pid_num, t_int(2)), 1u, 1u);
    set_pos(e, 1u, 0u, ORIGIN_EDB, f1(g_pid_num, t_int(20)), NO_STEP);
    set_step(e, 2u, 3u, f2(g_pid_allow, t_sym(g_sym_alice), t_sym(g_sym_doc)), 2u, 2u);
    set_pos(e, 2u, 0u, ORIGIN_IDB, f1(g_pid_num, t_int(1)), 0u);
    set_pos(e, 3u, 1u, ORIGIN_IDB, f1(g_pid_num, t_int(2)), 1u);
    finish(e, 3u, 4u);
}

static const char k_normative_expected[] =
    "MAELYS-DATALOG-WHY-TRUE-TEXT-v1\n"
    "status=complete\n"
    "steps=1 premises=2\n"
    "step=0 rule=7 fact=\"allow\"(\"alice\",\"doc.pdf\")\n"
    "premise=0 body=0 kind=positive origin=edb fact=\"owns\"(\"alice\",\"doc.pdf\") parent=-\n"
    "premise=1 body=1 kind=negative-absence origin=edb fact=\"blocked\"(\"alice\") parent=-\n"
    "result-step=0\n";

/* ---- shared assertions ---- */

static int format_golden_check(const maelys_datalog_ruleset_t *rs,
                               const maelys_datalog_explanation_t *e,
                               const char *expected) {
    const size_t expected_len = strlen(expected);
    size_t required = 0u;
    maelys_result_t rc =
        maelys_datalog_format_explanation_text(rs, e, NULL, 0u, &required);
    if (rc != MAELYS_OK) {
        fprintf(stderr, "golden: count-only rc=%d\n", (int)rc);
        return 0;
    }
    if (required != expected_len) {
        fprintf(stderr, "golden: required=%zu expected_len=%zu\n", required,
                expected_len);
        return 0;
    }
    memset(g_text_a, 0x11, expected_len + 2u);
    size_t written_required = 0u;
    rc = maelys_datalog_format_explanation_text(rs, e, g_text_a, required + 1u,
                                                &written_required);
    if (rc != MAELYS_OK) {
        fprintf(stderr, "golden: write rc=%d\n", (int)rc);
        return 0;
    }
    if (written_required != required) return 0;
    if (memcmp(g_text_a, expected, expected_len + 1u) != 0) {
        fprintf(stderr, "golden mismatch\nexpected:\n%s\nactual:\n%s\n", expected,
                g_text_a);
        return 0;
    }
    return 1;
}

/* §6.1(15)(16): one trailing LF before the NUL; no CR, no TAB, no empty
 * line, no trailing space, versioned header first. */
static int text_wellformed(const char *text, size_t len) {
    static const char header[] = "MAELYS-DATALOG-WHY-TRUE-TEXT-v1\n";
    if (len < sizeof(header) - 1u) return 0;
    if (memcmp(text, header, sizeof(header) - 1u) != 0) return 0;
    if (text[len - 1u] != '\n') return 0;
    if (text[len] != '\0') return 0;
    for (size_t i = 0; i < len; i++) {
        const char c = text[i];
        if (c == '\r' || c == '\t') return 0;
        if (c == '\n') {
            if (i == 0u) return 0;
            if (text[i - 1u] == '\n' || text[i - 1u] == ' ') return 0;
        }
    }
    return 1;
}

/* §6.3: exact error code, sentinel buffer byte-identical, out_required
 * unchanged, in count-only mode and in write mode. */
static int expect_error_untouched(const maelys_datalog_ruleset_t *rs,
                                  const maelys_datalog_explanation_t *e,
                                  maelys_result_t expected_rc) {
    size_t required = (size_t)0xDEADBEEFu;
    maelys_result_t rc =
        maelys_datalog_format_explanation_text(rs, e, NULL, 0u, &required);
    if (rc != expected_rc) {
        fprintf(stderr, "count-only rc=%d expected=%d\n", (int)rc, (int)expected_rc);
        return 0;
    }
    if (required != (size_t)0xDEADBEEFu) return 0;
    memset(g_text_a, 0x5A, 512u);
    required = (size_t)0xDEADBEEFu;
    rc = maelys_datalog_format_explanation_text(rs, e, g_text_a, 512u, &required);
    if (rc != expected_rc) {
        fprintf(stderr, "write rc=%d expected=%d\n", (int)rc, (int)expected_rc);
        return 0;
    }
    if (required != (size_t)0xDEADBEEFu) return 0;
    for (size_t i = 0; i < 512u; i++) {
        if ((unsigned char)g_text_a[i] != 0x5Au) return 0;
    }
    return 1;
}

/* ====================================================================
 * §6.1 — golden strings
 * ==================================================================== */

static int test_fmt_golden_not_derived(void) {
    TEST_BEGIN();
    TEST_ASSERT_TRUE(ensure_fx());
    memset(&g_exp, 0, sizeof(g_exp));
    static const char expected[] =
        "MAELYS-DATALOG-WHY-TRUE-TEXT-v1\n"
        "status=not-derived\n"
        "steps=0 premises=0\n";
    TEST_ASSERT_TRUE(format_golden_check(&g_fx, &g_exp, expected));
    TEST_ASSERT_TRUE(text_wellformed(g_text_a, sizeof(expected) - 1u));
    TEST_END();
}

static int test_fmt_golden_truncated(void) {
    TEST_BEGIN();
    TEST_ASSERT_TRUE(ensure_fx());
    memset(&g_exp, 0, sizeof(g_exp));
    g_exp.found = 1u;
    g_exp.truncated = 1u;
    static const char expected[] =
        "MAELYS-DATALOG-WHY-TRUE-TEXT-v1\n"
        "status=truncated\n"
        "steps=0 premises=0\n";
    TEST_ASSERT_TRUE(format_golden_check(&g_fx, &g_exp, expected));
    TEST_ASSERT_TRUE(text_wellformed(g_text_a, sizeof(expected) - 1u));
    TEST_END();
}

static int test_fmt_golden_normative_example(void) {
    TEST_BEGIN();
    TEST_ASSERT_TRUE(ensure_fx());
    build_normative_example(&g_exp);
    TEST_ASSERT_TRUE(format_golden_check(&g_fx, &g_exp, k_normative_expected));
    TEST_ASSERT_TRUE(text_wellformed(g_text_a, sizeof(k_normative_expected) - 1u));
    TEST_END();
}

static int test_fmt_golden_policy_fact_origin(void) {
    TEST_BEGIN();
    TEST_ASSERT_TRUE(ensure_fx());
    memset(&g_exp, 0, sizeof(g_exp));
    set_step(&g_exp, 0u, 1u, f2(g_pid_allow, t_sym(g_sym_alice), t_sym(g_sym_doc)),
             0u, 1u);
    set_pos(&g_exp, 0u, 0u, ORIGIN_POLICY, f1(g_pid_pol, t_sym(g_sym_alice)), NO_STEP);
    finish(&g_exp, 1u, 1u);
    static const char expected[] =
        "MAELYS-DATALOG-WHY-TRUE-TEXT-v1\n"
        "status=complete\n"
        "steps=1 premises=1\n"
        "step=0 rule=1 fact=\"allow\"(\"alice\",\"doc.pdf\")\n"
        "premise=0 body=0 kind=positive origin=policy-fact fact=\"pol\"(\"alice\") parent=-\n"
        "result-step=0\n";
    TEST_ASSERT_TRUE(format_golden_check(&g_fx, &g_exp, expected));
    TEST_END();
}

static int test_fmt_golden_two_distinct_idb_parents(void) {
    TEST_BEGIN();
    TEST_ASSERT_TRUE(ensure_fx());
    build_parent_chain(&g_exp);
    static const char expected[] =
        "MAELYS-DATALOG-WHY-TRUE-TEXT-v1\n"
        "status=complete\n"
        "steps=3 premises=4\n"
        "step=0 rule=1 fact=\"num\"(1)\n"
        "premise=0 body=0 kind=positive origin=edb fact=\"num\"(10) parent=-\n"
        "step=1 rule=2 fact=\"num\"(2)\n"
        "premise=1 body=0 kind=positive origin=edb fact=\"num\"(20) parent=-\n"
        "step=2 rule=3 fact=\"allow\"(\"alice\",\"doc.pdf\")\n"
        "premise=2 body=0 kind=positive origin=idb fact=\"num\"(1) parent=0\n"
        "premise=3 body=1 kind=positive origin=idb fact=\"num\"(2) parent=1\n"
        "result-step=2\n";
    TEST_ASSERT_TRUE(format_golden_check(&g_fx, &g_exp, expected));
    TEST_ASSERT_TRUE(text_wellformed(g_text_a, sizeof(expected) - 1u));
    TEST_END();
}

static int test_fmt_golden_shared_idb_parent(void) {
    TEST_BEGIN();
    TEST_ASSERT_TRUE(ensure_fx());
    memset(&g_exp, 0, sizeof(g_exp));
    set_step(&g_exp, 0u, 1u, f1(g_pid_num, t_int(1)), 0u, 1u);
    set_pos(&g_exp, 0u, 0u, ORIGIN_EDB, f1(g_pid_num, t_int(10)), NO_STEP);
    set_step(&g_exp, 1u, 2u, f1(g_pid_num, t_int(2)), 1u, 1u);
    set_pos(&g_exp, 1u, 0u, ORIGIN_IDB, f1(g_pid_num, t_int(1)), 0u);
    set_step(&g_exp, 2u, 3u, f1(g_pid_num, t_int(3)), 2u, 2u);
    set_pos(&g_exp, 2u, 0u, ORIGIN_IDB, f1(g_pid_num, t_int(1)), 0u);
    set_pos(&g_exp, 3u, 1u, ORIGIN_IDB, f1(g_pid_num, t_int(2)), 1u);
    finish(&g_exp, 3u, 4u);
    /* The shared parent step 0 is rendered once and referenced twice. */
    static const char expected[] =
        "MAELYS-DATALOG-WHY-TRUE-TEXT-v1\n"
        "status=complete\n"
        "steps=3 premises=4\n"
        "step=0 rule=1 fact=\"num\"(1)\n"
        "premise=0 body=0 kind=positive origin=edb fact=\"num\"(10) parent=-\n"
        "step=1 rule=2 fact=\"num\"(2)\n"
        "premise=1 body=0 kind=positive origin=idb fact=\"num\"(1) parent=0\n"
        "step=2 rule=3 fact=\"num\"(3)\n"
        "premise=2 body=0 kind=positive origin=idb fact=\"num\"(1) parent=0\n"
        "premise=3 body=1 kind=positive origin=idb fact=\"num\"(2) parent=1\n"
        "result-step=2\n";
    TEST_ASSERT_TRUE(format_golden_check(&g_fx, &g_exp, expected));
    TEST_END();
}

static int test_fmt_golden_comparison_six_ops(void) {
    TEST_BEGIN();
    TEST_ASSERT_TRUE(ensure_fx());
    memset(&g_exp, 0, sizeof(g_exp));
    set_step(&g_exp, 0u, 1u, f1(g_pid_num, t_int(0)), 0u, 6u);
    set_cmp(&g_exp, 0u, 0u, (uint8_t)MAELYS_DATALOG_CMP_EQ, t_int(1), t_int(1));
    set_cmp(&g_exp, 1u, 1u, (uint8_t)MAELYS_DATALOG_CMP_NEQ, t_int(1), t_int(2));
    set_cmp(&g_exp, 2u, 2u, (uint8_t)MAELYS_DATALOG_CMP_LT, t_int(1), t_int(2));
    set_cmp(&g_exp, 3u, 3u, (uint8_t)MAELYS_DATALOG_CMP_LTE, t_int(2), t_int(2));
    set_cmp(&g_exp, 4u, 4u, (uint8_t)MAELYS_DATALOG_CMP_GT, t_int(2), t_int(1));
    set_cmp(&g_exp, 5u, 5u, (uint8_t)MAELYS_DATALOG_CMP_GTE, t_int(2), t_int(1));
    finish(&g_exp, 1u, 6u);
    static const char expected[] =
        "MAELYS-DATALOG-WHY-TRUE-TEXT-v1\n"
        "status=complete\n"
        "steps=1 premises=6\n"
        "step=0 rule=1 fact=\"num\"(0)\n"
        "premise=0 body=0 kind=comparison-true origin=none lhs=1 op== rhs=1 parent=-\n"
        "premise=1 body=1 kind=comparison-true origin=none lhs=1 op=!= rhs=2 parent=-\n"
        "premise=2 body=2 kind=comparison-true origin=none lhs=1 op=< rhs=2 parent=-\n"
        "premise=3 body=3 kind=comparison-true origin=none lhs=2 op=<= rhs=2 parent=-\n"
        "premise=4 body=4 kind=comparison-true origin=none lhs=2 op=> rhs=1 parent=-\n"
        "premise=5 body=5 kind=comparison-true origin=none lhs=2 op=>= rhs=1 parent=-\n"
        "result-step=0\n";
    TEST_ASSERT_TRUE(format_golden_check(&g_fx, &g_exp, expected));
    TEST_END();
}

static int build_single_int_explanation(long long v) {
    memset(&g_exp, 0, sizeof(g_exp));
    set_step(&g_exp, 0u, 1u, f1(g_pid_num, t_int(v)), 0u, 1u);
    set_cmp(&g_exp, 0u, 0u, (uint8_t)MAELYS_DATALOG_CMP_EQ, t_int(v), t_int(v));
    finish(&g_exp, 1u, 1u);
    return 1;
}

static int check_int_golden(long long v, const char *decimal) {
    build_single_int_explanation(v);
    int n = snprintf(g_expected, sizeof(g_expected),
                     "MAELYS-DATALOG-WHY-TRUE-TEXT-v1\n"
                     "status=complete\n"
                     "steps=1 premises=1\n"
                     "step=0 rule=1 fact=\"num\"(%s)\n"
                     "premise=0 body=0 kind=comparison-true origin=none "
                     "lhs=%s op== rhs=%s parent=-\n"
                     "result-step=0\n",
                     decimal, decimal, decimal);
    if (n <= 0 || (size_t)n >= sizeof(g_expected)) return 0;
    return format_golden_check(&g_fx, &g_exp, g_expected);
}

static int test_fmt_golden_int_extremes(void) {
    TEST_BEGIN();
    TEST_ASSERT_TRUE(ensure_fx());
    TEST_ASSERT_TRUE(check_int_golden(0LL, "0"));
    TEST_ASSERT_TRUE(check_int_golden(-7LL, "-7"));
    TEST_ASSERT_TRUE(check_int_golden(INT64_MIN, "-9223372036854775808"));
    TEST_ASSERT_TRUE(check_int_golden(INT64_MAX, "9223372036854775807"));
    TEST_END();
}

static int test_fmt_golden_bool(void) {
    TEST_BEGIN();
    TEST_ASSERT_TRUE(ensure_fx());
    memset(&g_exp, 0, sizeof(g_exp));
    set_step(&g_exp, 0u, 1u, f1(g_pid_flag, t_bool(1)), 0u, 1u);
    set_cmp(&g_exp, 0u, 0u, (uint8_t)MAELYS_DATALOG_CMP_NEQ, t_bool(1), t_bool(0));
    finish(&g_exp, 1u, 1u);
    static const char expected_true[] =
        "MAELYS-DATALOG-WHY-TRUE-TEXT-v1\n"
        "status=complete\n"
        "steps=1 premises=1\n"
        "step=0 rule=1 fact=\"flag\"(true)\n"
        "premise=0 body=0 kind=comparison-true origin=none lhs=true op=!= rhs=false parent=-\n"
        "result-step=0\n";
    TEST_ASSERT_TRUE(format_golden_check(&g_fx, &g_exp, expected_true));

    memset(&g_exp, 0, sizeof(g_exp));
    set_step(&g_exp, 0u, 1u, f1(g_pid_flag, t_bool(0)), 0u, 1u);
    set_pos(&g_exp, 0u, 0u, ORIGIN_EDB, f1(g_pid_flag, t_bool(0)), NO_STEP);
    finish(&g_exp, 1u, 1u);
    static const char expected_false[] =
        "MAELYS-DATALOG-WHY-TRUE-TEXT-v1\n"
        "status=complete\n"
        "steps=1 premises=1\n"
        "step=0 rule=1 fact=\"flag\"(false)\n"
        "premise=0 body=0 kind=positive origin=edb fact=\"flag\"(false) parent=-\n"
        "result-step=0\n";
    TEST_ASSERT_TRUE(format_golden_check(&g_fx, &g_exp, expected_false));
    TEST_END();
}

static int test_fmt_golden_zero_arity(void) {
    TEST_BEGIN();
    TEST_ASSERT_TRUE(ensure_fx());
    memset(&g_exp, 0, sizeof(g_exp));
    set_step(&g_exp, 0u, 1u, f0(g_pid_zero), 0u, 1u);
    set_pos(&g_exp, 0u, 0u, ORIGIN_EDB, f0(g_pid_zero), NO_STEP);
    finish(&g_exp, 1u, 1u);
    static const char expected[] =
        "MAELYS-DATALOG-WHY-TRUE-TEXT-v1\n"
        "status=complete\n"
        "steps=1 premises=1\n"
        "step=0 rule=1 fact=\"zero\"()\n"
        "premise=0 body=0 kind=positive origin=edb fact=\"zero\"() parent=-\n"
        "result-step=0\n";
    TEST_ASSERT_TRUE(format_golden_check(&g_fx, &g_exp, expected));
    TEST_END();
}

static int check_str_golden(maelys_datalog_symbol_id_t sym, const char *rendered) {
    memset(&g_exp, 0, sizeof(g_exp));
    set_step(&g_exp, 0u, 1u, f1(g_pid_str, t_sym(sym)), 0u, 1u);
    set_cmp(&g_exp, 0u, 0u, (uint8_t)MAELYS_DATALOG_CMP_EQ, t_int(1), t_int(1));
    finish(&g_exp, 1u, 1u);
    int n = snprintf(g_expected, sizeof(g_expected),
                     "MAELYS-DATALOG-WHY-TRUE-TEXT-v1\n"
                     "status=complete\n"
                     "steps=1 premises=1\n"
                     "step=0 rule=1 fact=\"str\"(%s)\n"
                     "premise=0 body=0 kind=comparison-true origin=none "
                     "lhs=1 op== rhs=1 parent=-\n"
                     "result-step=0\n",
                     rendered);
    if (n <= 0 || (size_t)n >= sizeof(g_expected)) return 0;
    return format_golden_check(&g_fx, &g_exp, g_expected);
}

static int test_fmt_golden_ascii_escapes(void) {
    TEST_BEGIN();
    TEST_ASSERT_TRUE(ensure_fx());
    /* Plain ASCII stays readable. */
    TEST_ASSERT_TRUE(check_str_golden(g_sym_alice, "\"alice\""));
    /* Quote and backslash keep their named escapes. */
    TEST_ASSERT_TRUE(check_str_golden(g_sym_qslash, "\"quote\\\"slash\\\\\""));
    /* LF as \n (valid-UTF-8 mode). */
    TEST_ASSERT_TRUE(check_str_golden(g_sym_lf, "\"line\\nbreak\""));
    /* TAB as \t and CR as \r. */
    TEST_ASSERT_TRUE(check_str_golden(g_sym_tabcr, "\"tab\\tcr\\rx\""));
    /* Embedded NUL is preserved as \x00, never a truncation point. */
    TEST_ASSERT_TRUE(check_str_golden(g_sym_nul, "\"nul\\x00inside\""));
    /* Another ASCII control (BEL) and DEL as uppercase \xHH. */
    TEST_ASSERT_TRUE(check_str_golden(g_sym_ctl, "\"bell\\x07del\\x7F\""));
    TEST_END();
}

static int test_fmt_golden_utf8_non_ascii(void) {
    TEST_BEGIN();
    TEST_ASSERT_TRUE(ensure_fx());
    /* Valid UTF-8 stays byte-identical: the output holds the bytes C3 A9. */
    TEST_ASSERT_TRUE(check_str_golden(g_sym_cafe, "\"caf\xC3\xA9\""));
    TEST_END();
}

static int test_fmt_golden_invalid_utf8(void) {
    TEST_BEGIN();
    TEST_ASSERT_TRUE(ensure_fx());
    /* A globally invalid sequence renders every non-printable byte as \xHH. */
    TEST_ASSERT_TRUE(check_str_golden(g_sym_bad, "\"\\xFFbad\""));
    /* In invalid mode, LF/CR/TAB follow the byte mode (\x0A \x0D \x09),
     * not the named escapes of the valid-UTF-8 mode. */
    TEST_ASSERT_TRUE(check_str_golden(g_sym_badmix, "\"x\\xFF\\x0A\\x0D\\x09\""));
    TEST_END();
}

static int test_fmt_golden_max_lengths(void) {
    TEST_BEGIN();
    TEST_ASSERT_TRUE(ensure_fx());
    memset(&g_exp, 0, sizeof(g_exp));
    set_step(&g_exp, 0u, 1u, f1(g_pid_maxname, t_sym(g_sym_max)), 0u, 1u);
    set_cmp(&g_exp, 0u, 0u, (uint8_t)MAELYS_DATALOG_CMP_EQ, t_int(0), t_int(0));
    finish(&g_exp, 1u, 1u);
    /* Expected text built programmatically: 63-byte predicate name and a
     * MAX_STRING_BYTES symbol, both quoted verbatim. */
    size_t off = 0u;
    static const char head[] =
        "MAELYS-DATALOG-WHY-TRUE-TEXT-v1\n"
        "status=complete\n"
        "steps=1 premises=1\n"
        "step=0 rule=1 fact=\"";
    memcpy(g_expected + off, head, sizeof(head) - 1u);
    off += sizeof(head) - 1u;
    memcpy(g_expected + off, g_maxname, 63u);
    off += 63u;
    memcpy(g_expected + off, "\"(\"", 3u);
    off += 3u;
    memcpy(g_expected + off, g_maxsym, sizeof(g_maxsym));
    off += sizeof(g_maxsym);
    static const char tail[] =
        "\")\n"
        "premise=0 body=0 kind=comparison-true origin=none lhs=0 op== rhs=0 parent=-\n"
        "result-step=0\n";
    memcpy(g_expected + off, tail, sizeof(tail) - 1u);
    off += sizeof(tail) - 1u;
    g_expected[off] = '\0';
    TEST_ASSERT_TRUE(format_golden_check(&g_fx, &g_exp, g_expected));
    TEST_ASSERT_TRUE(text_wellformed(g_text_a, off));
    TEST_END();
}

static int test_fmt_all_lines_hygiene(void) {
    TEST_BEGIN();
    TEST_ASSERT_TRUE(ensure_fx());
    /* Re-render a representative set and prove §6.1(15)(16) on each. */
    build_normative_example(&g_exp);
    size_t required = 0u;
    TEST_ASSERT_EQUAL(MAELYS_OK,
                      maelys_datalog_format_explanation_text(
                          &g_fx, &g_exp, g_text_a, sizeof(g_text_a), &required),
                      "%d");
    TEST_ASSERT_TRUE(text_wellformed(g_text_a, required));
    /* Exactly one LF terminates the text: the byte before the final LF is
     * never itself an LF (checked by text_wellformed), and the final LF is
     * immediately followed by the NUL. */
    TEST_ASSERT_TRUE(required >= 2u && g_text_a[required - 1u] == '\n' &&
                     g_text_a[required - 2u] != '\n');

    build_parent_chain(&g_exp);
    TEST_ASSERT_EQUAL(MAELYS_OK,
                      maelys_datalog_format_explanation_text(
                          &g_fx, &g_exp, g_text_a, sizeof(g_text_a), &required),
                      "%d");
    TEST_ASSERT_TRUE(text_wellformed(g_text_a, required));

    memset(&g_exp, 0, sizeof(g_exp));
    TEST_ASSERT_EQUAL(MAELYS_OK,
                      maelys_datalog_format_explanation_text(
                          &g_fx, &g_exp, g_text_a, sizeof(g_text_a), &required),
                      "%d");
    TEST_ASSERT_TRUE(text_wellformed(g_text_a, required));
    TEST_END();
}

/* ====================================================================
 * §6.2 — buffer and atomicity
 * ==================================================================== */

static int test_fmt_count_only_exact(void) {
    TEST_BEGIN();
    TEST_ASSERT_TRUE(ensure_fx());
    build_normative_example(&g_exp);
    size_t required = 0u;
    TEST_ASSERT_EQUAL(MAELYS_OK,
                      maelys_datalog_format_explanation_text(&g_fx, &g_exp, NULL, 0u,
                                                             &required),
                      "%d");
    TEST_ASSERT_EQUAL(sizeof(k_normative_expected) - 1u, required, "%zu");
    /* Runtime confirmation of the static bound used by the SIZE_MAX proof. */
    TEST_ASSERT_TRUE(required < (size_t)FMT_T_MAX_TEXT);
    TEST_END();
}

static int test_fmt_capacity_required_plus_one_succeeds(void) {
    TEST_BEGIN();
    TEST_ASSERT_TRUE(ensure_fx());
    build_normative_example(&g_exp);
    const size_t required = sizeof(k_normative_expected) - 1u;
    memset(g_text_a, 0x22, sizeof(k_normative_expected) + 8u);
    size_t out_required = 0u;
    TEST_ASSERT_EQUAL(MAELYS_OK,
                      maelys_datalog_format_explanation_text(
                          &g_fx, &g_exp, g_text_a, required + 1u, &out_required),
                      "%d");
    TEST_ASSERT_EQUAL(required, out_required, "%zu");
    TEST_ASSERT_EQUAL(0, memcmp(g_text_a, k_normative_expected, required + 1u), "%d");
    /* No byte beyond the NUL was written. */
    TEST_ASSERT_EQUAL(0x22, (int)(unsigned char)g_text_a[required + 1u], "%d");
    TEST_END();
}

static int test_fmt_capacity_required_fails_atomic(void) {
    TEST_BEGIN();
    TEST_ASSERT_TRUE(ensure_fx());
    build_normative_example(&g_exp);
    const size_t required = sizeof(k_normative_expected) - 1u;
    memset(g_text_a, 0x33, required + 8u);
    size_t out_required = 0u;
    TEST_ASSERT_EQUAL(MAELYS_ERR_PAYLOAD_TOO_LARGE,
                      maelys_datalog_format_explanation_text(
                          &g_fx, &g_exp, g_text_a, required, &out_required),
                      "%d");
    TEST_ASSERT_EQUAL(required, out_required, "%zu");
    TEST_ASSERT_EQUAL('\0', g_text_a[0], "%d");
    for (size_t i = 1; i < required + 8u; i++) {
        TEST_ASSERT_EQUAL(0x33, (int)(unsigned char)g_text_a[i], "%d");
        if ((unsigned char)g_text_a[i] != 0x33u) break;
    }
    TEST_END();
}

static int test_fmt_insufficient_capacities_never_prefix(void) {
    TEST_BEGIN();
    TEST_ASSERT_TRUE(ensure_fx());
    build_normative_example(&g_exp);
    const size_t required = sizeof(k_normative_expected) - 1u;
    for (size_t capacity = 1u; capacity <= required; capacity++) {
        memset(g_text_a, 0x44, required + 2u);
        size_t out_required = 0u;
        const maelys_result_t call_rc = maelys_datalog_format_explanation_text(
            &g_fx, &g_exp, g_text_a, capacity, &out_required);
        TEST_ASSERT_EQUAL(MAELYS_ERR_PAYLOAD_TOO_LARGE, call_rc, "%d");
        TEST_ASSERT_EQUAL(required, out_required, "%zu");
        TEST_ASSERT_EQUAL('\0', g_text_a[0], "%d");
        int intact = 1;
        for (size_t i = 1; i < required + 2u; i++) {
            if ((unsigned char)g_text_a[i] != 0x44u) {
                intact = 0;
                break;
            }
        }
        TEST_ASSERT_TRUE(intact);
        if (call_rc != MAELYS_ERR_PAYLOAD_TOO_LARGE || !intact) break;
    }
    /* Capacity zero with a non-NULL buffer: exact size, buffer untouched. */
    memset(g_text_a, 0x44, 4u);
    size_t out_required = 0u;
    TEST_ASSERT_EQUAL(MAELYS_ERR_PAYLOAD_TOO_LARGE,
                      maelys_datalog_format_explanation_text(&g_fx, &g_exp, g_text_a,
                                                             0u, &out_required),
                      "%d");
    TEST_ASSERT_EQUAL(required, out_required, "%zu");
    TEST_ASSERT_EQUAL(0x44, (int)(unsigned char)g_text_a[0], "%d");
    TEST_END();
}

static int test_fmt_two_sufficient_sizes_identical(void) {
    TEST_BEGIN();
    TEST_ASSERT_TRUE(ensure_fx());
    build_parent_chain(&g_exp);
    size_t required = 0u;
    TEST_ASSERT_EQUAL(MAELYS_OK,
                      maelys_datalog_format_explanation_text(&g_fx, &g_exp, NULL, 0u,
                                                             &required),
                      "%d");
    memset(g_text_a, 0x66, sizeof(g_text_a));
    memset(g_text_b, 0x77, sizeof(g_text_b));
    size_t req_a = 0u;
    size_t req_b = 0u;
    TEST_ASSERT_EQUAL(MAELYS_OK,
                      maelys_datalog_format_explanation_text(
                          &g_fx, &g_exp, g_text_a, required + 1u, &req_a),
                      "%d");
    TEST_ASSERT_EQUAL(MAELYS_OK,
                      maelys_datalog_format_explanation_text(
                          &g_fx, &g_exp, g_text_b, sizeof(g_text_b), &req_b),
                      "%d");
    TEST_ASSERT_EQUAL(required, req_a, "%zu");
    TEST_ASSERT_EQUAL(required, req_b, "%zu");
    TEST_ASSERT_EQUAL(0, memcmp(g_text_a, g_text_b, required + 1u), "%d");
    TEST_END();
}

/* ====================================================================
 * §6.3 — defensive validation matrix
 * ==================================================================== */

static int test_fmt_contract_null_arguments(void) {
    TEST_BEGIN();
    TEST_ASSERT_TRUE(ensure_fx());
    build_normative_example(&g_exp);
    size_t required = (size_t)0xDEADBEEFu;
    TEST_ASSERT_EQUAL(MAELYS_ERR_INVALID_ARGUMENT,
                      maelys_datalog_format_explanation_text(NULL, &g_exp, NULL, 0u,
                                                             &required),
                      "%d");
    TEST_ASSERT_EQUAL((size_t)0xDEADBEEFu, required, "%zu");
    TEST_ASSERT_EQUAL(MAELYS_ERR_INVALID_ARGUMENT,
                      maelys_datalog_format_explanation_text(&g_fx, NULL, NULL, 0u,
                                                             &required),
                      "%d");
    TEST_ASSERT_EQUAL((size_t)0xDEADBEEFu, required, "%zu");
    TEST_ASSERT_EQUAL(MAELYS_ERR_INVALID_ARGUMENT,
                      maelys_datalog_format_explanation_text(&g_fx, &g_exp, NULL, 0u,
                                                             NULL),
                      "%d");
    /* out_capacity > 0 with out_text == NULL. */
    TEST_ASSERT_EQUAL(MAELYS_ERR_INVALID_ARGUMENT,
                      maelys_datalog_format_explanation_text(&g_fx, &g_exp, NULL, 16u,
                                                             &required),
                      "%d");
    TEST_ASSERT_EQUAL((size_t)0xDEADBEEFu, required, "%zu");
    /* Sentinel buffer stays untouched on a NULL explanation. */
    memset(g_text_a, 0x5A, 64u);
    TEST_ASSERT_EQUAL(MAELYS_ERR_INVALID_ARGUMENT,
                      maelys_datalog_format_explanation_text(&g_fx, NULL, g_text_a,
                                                             64u, &required),
                      "%d");
    for (size_t i = 0; i < 64u; i++) {
        TEST_ASSERT_EQUAL(0x5A, (int)(unsigned char)g_text_a[i], "%d");
        if ((unsigned char)g_text_a[i] != 0x5Au) break;
    }
    TEST_END();
}

static int test_fmt_invalid_state_ruleset(void) {
    TEST_BEGIN();
    TEST_ASSERT_TRUE(ensure_fx());
    build_normative_example(&g_exp);
    /* Ruleset not loaded. */
    memcpy(&g_bad_rs, &g_fx, sizeof(g_fx));
    g_bad_rs.loaded = 0;
    TEST_ASSERT_TRUE(expect_error_untouched(&g_bad_rs, &g_exp, MAELYS_ERR_INVALID_STATE));
    /* Registry not frozen. */
    memcpy(&g_bad_rs, &g_fx, sizeof(g_fx));
    g_bad_rs.registry.frozen = 0;
    TEST_ASSERT_TRUE(expect_error_untouched(&g_bad_rs, &g_exp, MAELYS_ERR_INVALID_STATE));
    TEST_END();
}

static int test_fmt_invalid_found_truncated_matrix(void) {
    TEST_BEGIN();
    TEST_ASSERT_TRUE(ensure_fx());

    /* found=0 with truncated=1. */
    memset(&g_bad, 0, sizeof(g_bad));
    g_bad.truncated = 1u;
    TEST_ASSERT_TRUE(expect_error_untouched(&g_fx, &g_bad, MAELYS_ERR_INVALID_FIELD));
    /* found=0 with steps. */
    build_normative_example(&g_bad);
    g_bad.found = 0u;
    TEST_ASSERT_TRUE(expect_error_untouched(&g_fx, &g_bad, MAELYS_ERR_INVALID_FIELD));
    /* found=0 with premises only. */
    memset(&g_bad, 0, sizeof(g_bad));
    g_bad.premise_count = 1u;
    TEST_ASSERT_TRUE(expect_error_untouched(&g_fx, &g_bad, MAELYS_ERR_INVALID_FIELD));
    /* found=1, truncated=1 with steps. */
    build_normative_example(&g_bad);
    g_bad.truncated = 1u;
    TEST_ASSERT_TRUE(expect_error_untouched(&g_fx, &g_bad, MAELYS_ERR_INVALID_FIELD));
    /* found=1, truncated=1 with premises only. */
    memset(&g_bad, 0, sizeof(g_bad));
    g_bad.found = 1u;
    g_bad.truncated = 1u;
    g_bad.premise_count = 2u;
    TEST_ASSERT_TRUE(expect_error_untouched(&g_fx, &g_bad, MAELYS_ERR_INVALID_FIELD));
    /* found=1, truncated=0, step_count=0. */
    memset(&g_bad, 0, sizeof(g_bad));
    g_bad.found = 1u;
    TEST_ASSERT_TRUE(expect_error_untouched(&g_fx, &g_bad, MAELYS_ERR_INVALID_FIELD));
    /* Unknown found / truncated values. */
    build_normative_example(&g_bad);
    g_bad.found = 2u;
    TEST_ASSERT_TRUE(expect_error_untouched(&g_fx, &g_bad, MAELYS_ERR_INVALID_FIELD));
    build_normative_example(&g_bad);
    g_bad.truncated = 3u;
    TEST_ASSERT_TRUE(expect_error_untouched(&g_fx, &g_bad, MAELYS_ERR_INVALID_FIELD));
    /* Counts beyond the public bounds. */
    build_normative_example(&g_bad);
    g_bad.step_count = (uint16_t)(MAELYS_DATALOG_MAX_EXPLANATION_STEPS + 1u);
    TEST_ASSERT_TRUE(expect_error_untouched(&g_fx, &g_bad, MAELYS_ERR_INVALID_FIELD));
    build_normative_example(&g_bad);
    g_bad.premise_count = (uint16_t)(MAELYS_DATALOG_MAX_EXPLANATION_PREMISES + 1u);
    TEST_ASSERT_TRUE(expect_error_untouched(&g_fx, &g_bad, MAELYS_ERR_INVALID_FIELD));
    TEST_END();
}

static int test_fmt_invalid_ranges_and_body_indices(void) {
    TEST_BEGIN();
    TEST_ASSERT_TRUE(ensure_fx());
    /* Hole before the first range. */
    build_normative_example(&g_bad);
    g_bad.steps[0].premise_begin = 1u;
    TEST_ASSERT_TRUE(expect_error_untouched(&g_fx, &g_bad, MAELYS_ERR_INVALID_FIELD));
    /* Overlap between consecutive ranges. */
    build_parent_chain(&g_bad);
    g_bad.steps[1].premise_begin = 0u;
    TEST_ASSERT_TRUE(expect_error_untouched(&g_fx, &g_bad, MAELYS_ERR_INVALID_FIELD));
    /* Uncovered tail: ranges do not reach premise_count. */
    build_normative_example(&g_bad);
    g_bad.steps[0].premise_count = 1u;
    TEST_ASSERT_TRUE(expect_error_untouched(&g_fx, &g_bad, MAELYS_ERR_INVALID_FIELD));
    /* Range beyond premise_count. */
    build_normative_example(&g_bad);
    g_bad.steps[0].premise_count = 3u;
    TEST_ASSERT_TRUE(expect_error_untouched(&g_fx, &g_bad, MAELYS_ERR_INVALID_FIELD));
    /* Non-lexical body index. */
    build_normative_example(&g_bad);
    g_bad.premises[1].body_index = 0u;
    TEST_ASSERT_TRUE(expect_error_untouched(&g_fx, &g_bad, MAELYS_ERR_INVALID_FIELD));
    TEST_END();
}

static int test_fmt_invalid_rule_ids_and_vocabulary(void) {
    TEST_BEGIN();
    TEST_ASSERT_TRUE(ensure_fx());
    /* Rule id zero. */
    build_normative_example(&g_bad);
    g_bad.steps[0].rule_id = 0u;
    TEST_ASSERT_TRUE(expect_error_untouched(&g_fx, &g_bad, MAELYS_ERR_INVALID_FIELD));
    /* Rule id out of bounds (rule_count + 1). */
    build_normative_example(&g_bad);
    g_bad.steps[0].rule_id = g_fx.rule_count + 1u;
    TEST_ASSERT_TRUE(expect_error_untouched(&g_fx, &g_bad, MAELYS_ERR_INVALID_FIELD));
    /* Rule entry incoherent with its 1-based id (corrupted ruleset copy). */
    build_normative_example(&g_bad);
    g_bad.steps[0].rule_id = 1u;
    memcpy(&g_bad_rs, &g_fx, sizeof(g_fx));
    g_bad_rs.rules[0].rule_id = 99u;
    TEST_ASSERT_TRUE(expect_error_untouched(&g_bad_rs, &g_bad, MAELYS_ERR_INVALID_FIELD));
    /* Unknown predicate id. */
    build_normative_example(&g_bad);
    g_bad.steps[0].derived_fact.predicate_id =
        (maelys_datalog_predicate_id_t)g_fx.registry.count;
    TEST_ASSERT_TRUE(expect_error_untouched(&g_fx, &g_bad, MAELYS_ERR_INVALID_FIELD));
    /* Arity incoherent with the registry. */
    build_normative_example(&g_bad);
    g_bad.steps[0].derived_fact.arity = 1u;
    TEST_ASSERT_TRUE(expect_error_untouched(&g_fx, &g_bad, MAELYS_ERR_INVALID_FIELD));
    /* Arity beyond the public bound. */
    build_normative_example(&g_bad);
    g_bad.premises[0].as.fact.arity = (uint8_t)(MAELYS_DATALOG_MAX_ARITY + 1u);
    TEST_ASSERT_TRUE(expect_error_untouched(&g_fx, &g_bad, MAELYS_ERR_INVALID_FIELD));
    /* Predicate name without a NUL inside its declared capacity. */
    build_normative_example(&g_bad);
    memcpy(&g_bad_rs, &g_fx, sizeof(g_fx));
    memset(g_bad_rs.registry.defs[g_pid_allow].name, 'A',
           sizeof(g_bad_rs.registry.defs[g_pid_allow].name));
    TEST_ASSERT_TRUE(expect_error_untouched(&g_bad_rs, &g_bad, MAELYS_ERR_INVALID_FIELD));
    TEST_END();
}

static int test_fmt_invalid_terms(void) {
    TEST_BEGIN();
    TEST_ASSERT_TRUE(ensure_fx());
    /* VAR term in a ground explanation. */
    build_normative_example(&g_bad);
    g_bad.steps[0].derived_fact.terms[0].kind = MAELYS_DATALOG_TERM_VAR;
    g_bad.steps[0].derived_fact.terms[0].as.variable = 0u;
    TEST_ASSERT_TRUE(expect_error_untouched(&g_fx, &g_bad, MAELYS_ERR_INVALID_FIELD));
    /* Unknown term kinds. */
    build_normative_example(&g_bad);
    g_bad.premises[0].as.fact.terms[0].kind = (maelys_datalog_term_kind_t)0;
    TEST_ASSERT_TRUE(expect_error_untouched(&g_fx, &g_bad, MAELYS_ERR_INVALID_FIELD));
    build_normative_example(&g_bad);
    g_bad.premises[0].as.fact.terms[1].kind = (maelys_datalog_term_kind_t)9;
    TEST_ASSERT_TRUE(expect_error_untouched(&g_fx, &g_bad, MAELYS_ERR_INVALID_FIELD));
    /* Symbol id zero: rejected before any id - 1 evaluation. */
    build_normative_example(&g_bad);
    g_bad.steps[0].derived_fact.terms[0].as.symbol = 0u;
    TEST_ASSERT_TRUE(expect_error_untouched(&g_fx, &g_bad, MAELYS_ERR_INVALID_FIELD));
    /* Symbol id out of range. */
    build_normative_example(&g_bad);
    g_bad.steps[0].derived_fact.terms[0].as.symbol =
        (maelys_datalog_symbol_id_t)(g_fx.symbols.count + 1u);
    TEST_ASSERT_TRUE(expect_error_untouched(&g_fx, &g_bad, MAELYS_ERR_INVALID_FIELD));
    TEST_END();
}

static int test_fmt_invalid_premises_and_links(void) {
    TEST_BEGIN();
    TEST_ASSERT_TRUE(ensure_fx());
    /* Unknown premise kind. */
    build_normative_example(&g_bad);
    g_bad.premises[0].kind = 0u;
    TEST_ASSERT_TRUE(expect_error_untouched(&g_fx, &g_bad, MAELYS_ERR_INVALID_FIELD));
    build_normative_example(&g_bad);
    g_bad.premises[0].kind = 4u;
    TEST_ASSERT_TRUE(expect_error_untouched(&g_fx, &g_bad, MAELYS_ERR_INVALID_FIELD));
    /* Unknown origin. */
    build_normative_example(&g_bad);
    g_bad.premises[0].origin = 0u;
    TEST_ASSERT_TRUE(expect_error_untouched(&g_fx, &g_bad, MAELYS_ERR_INVALID_FIELD));
    build_normative_example(&g_bad);
    g_bad.premises[0].origin = 5u;
    TEST_ASSERT_TRUE(expect_error_untouched(&g_fx, &g_bad, MAELYS_ERR_INVALID_FIELD));
    /* Positive premise with origin=none. */
    build_normative_example(&g_bad);
    g_bad.premises[0].origin = ORIGIN_NA;
    TEST_ASSERT_TRUE(expect_error_untouched(&g_fx, &g_bad, MAELYS_ERR_INVALID_FIELD));
    /* Non-zero op on a positive premise. */
    build_normative_example(&g_bad);
    g_bad.premises[0].op = (uint8_t)MAELYS_DATALOG_CMP_EQ;
    TEST_ASSERT_TRUE(expect_error_untouched(&g_fx, &g_bad, MAELYS_ERR_INVALID_FIELD));
    /* Parent present on a positive EDB premise. */
    build_normative_example(&g_bad);
    g_bad.premises[0].parent_step = 0u;
    TEST_ASSERT_TRUE(expect_error_untouched(&g_fx, &g_bad, MAELYS_ERR_INVALID_FIELD));
    /* Parent present on a positive POLICY_FACT premise. */
    build_normative_example(&g_bad);
    g_bad.premises[0].origin = ORIGIN_POLICY;
    g_bad.premises[0].parent_step = 0u;
    TEST_ASSERT_TRUE(expect_error_untouched(&g_fx, &g_bad, MAELYS_ERR_INVALID_FIELD));
    /* Parent present on a negated absence. */
    build_normative_example(&g_bad);
    g_bad.premises[1].parent_step = 0u;
    TEST_ASSERT_TRUE(expect_error_untouched(&g_fx, &g_bad, MAELYS_ERR_INVALID_FIELD));
    /* Negated absence with origin=none. */
    build_normative_example(&g_bad);
    g_bad.premises[1].origin = ORIGIN_NA;
    TEST_ASSERT_TRUE(expect_error_untouched(&g_fx, &g_bad, MAELYS_ERR_INVALID_FIELD));
    /* Comparison corruptions. */
    memset(&g_bad, 0, sizeof(g_bad));
    set_step(&g_bad, 0u, 1u, f1(g_pid_num, t_int(0)), 0u, 1u);
    set_cmp(&g_bad, 0u, 0u, (uint8_t)MAELYS_DATALOG_CMP_EQ, t_int(1), t_int(1));
    finish(&g_bad, 1u, 1u);
    g_bad.premises[0].op = 0u; /* unknown op */
    TEST_ASSERT_TRUE(expect_error_untouched(&g_fx, &g_bad, MAELYS_ERR_INVALID_FIELD));
    g_bad.premises[0].op = 7u; /* unknown op */
    TEST_ASSERT_TRUE(expect_error_untouched(&g_fx, &g_bad, MAELYS_ERR_INVALID_FIELD));
    g_bad.premises[0].op = (uint8_t)MAELYS_DATALOG_CMP_EQ;
    g_bad.premises[0].origin = ORIGIN_EDB; /* comparison must be origin=none */
    TEST_ASSERT_TRUE(expect_error_untouched(&g_fx, &g_bad, MAELYS_ERR_INVALID_FIELD));
    g_bad.premises[0].origin = ORIGIN_NA;
    g_bad.premises[0].parent_step = 0u; /* comparison with a parent */
    TEST_ASSERT_TRUE(expect_error_untouched(&g_fx, &g_bad, MAELYS_ERR_INVALID_FIELD));
    g_bad.premises[0].parent_step = NO_STEP;
    g_bad.premises[0].as.comparison.lhs.kind = MAELYS_DATALOG_TERM_VAR; /* non-ground */
    TEST_ASSERT_TRUE(expect_error_untouched(&g_fx, &g_bad, MAELYS_ERR_INVALID_FIELD));
    /* IDB parent corruptions. */
    build_parent_chain(&g_bad);
    g_bad.premises[2].parent_step = NO_STEP;
    TEST_ASSERT_TRUE(expect_error_untouched(&g_fx, &g_bad, MAELYS_ERR_INVALID_FIELD));
    build_parent_chain(&g_bad);
    g_bad.premises[2].parent_step = 2u; /* self */
    TEST_ASSERT_TRUE(expect_error_untouched(&g_fx, &g_bad, MAELYS_ERR_INVALID_FIELD));
    build_parent_chain(&g_bad);
    g_bad.premises[2].parent_step = 5u; /* forward / out of bounds */
    TEST_ASSERT_TRUE(expect_error_untouched(&g_fx, &g_bad, MAELYS_ERR_INVALID_FIELD));
    build_parent_chain(&g_bad);
    g_bad.premises[2].as.fact = f1(g_pid_num, t_int(999)); /* fact mismatch */
    TEST_ASSERT_TRUE(expect_error_untouched(&g_fx, &g_bad, MAELYS_ERR_INVALID_FIELD));
    /* Foreign step: an earlier step not referenced by the result DAG. */
    memset(&g_bad, 0, sizeof(g_bad));
    set_step(&g_bad, 0u, 1u, f1(g_pid_num, t_int(1)), 0u, 1u);
    set_pos(&g_bad, 0u, 0u, ORIGIN_EDB, f1(g_pid_num, t_int(10)), NO_STEP);
    set_step(&g_bad, 1u, 2u, f1(g_pid_num, t_int(2)), 1u, 1u);
    set_pos(&g_bad, 1u, 0u, ORIGIN_EDB, f1(g_pid_num, t_int(20)), NO_STEP);
    finish(&g_bad, 2u, 2u);
    TEST_ASSERT_TRUE(expect_error_untouched(&g_fx, &g_bad, MAELYS_ERR_INVALID_FIELD));
    TEST_END();
}

/* ====================================================================
 * §6.4 — non-mutation and determinism
 * ==================================================================== */

static int test_fmt_non_mutation_bitwise(void) {
    TEST_BEGIN();
    TEST_ASSERT_TRUE(ensure_fx());
    build_normative_example(&g_exp);
    memcpy(&g_fx_copy, &g_fx, sizeof(g_fx));
    memcpy(&g_exp_copy, &g_exp, sizeof(g_exp));
    /* Count-only. */
    size_t required = 0u;
    TEST_ASSERT_EQUAL(MAELYS_OK,
                      maelys_datalog_format_explanation_text(&g_fx, &g_exp, NULL, 0u,
                                                             &required),
                      "%d");
    TEST_ASSERT_EQUAL(0, memcmp(&g_fx_copy, &g_fx, sizeof(g_fx)), "%d");
    TEST_ASSERT_EQUAL(0, memcmp(&g_exp_copy, &g_exp, sizeof(g_exp)), "%d");
    /* Write. */
    TEST_ASSERT_EQUAL(MAELYS_OK,
                      maelys_datalog_format_explanation_text(
                          &g_fx, &g_exp, g_text_a, sizeof(g_text_a), &required),
                      "%d");
    TEST_ASSERT_EQUAL(0, memcmp(&g_fx_copy, &g_fx, sizeof(g_fx)), "%d");
    TEST_ASSERT_EQUAL(0, memcmp(&g_exp_copy, &g_exp, sizeof(g_exp)), "%d");
    TEST_END();
}

static int test_fmt_two_calls_identical(void) {
    TEST_BEGIN();
    TEST_ASSERT_TRUE(ensure_fx());
    build_parent_chain(&g_exp);
    size_t req_a = 0u;
    size_t req_b = 0u;
    memset(g_text_a, 0x01, sizeof(g_text_a));
    memset(g_text_b, 0x02, sizeof(g_text_b));
    TEST_ASSERT_EQUAL(MAELYS_OK,
                      maelys_datalog_format_explanation_text(
                          &g_fx, &g_exp, g_text_a, sizeof(g_text_a), &req_a),
                      "%d");
    TEST_ASSERT_EQUAL(MAELYS_OK,
                      maelys_datalog_format_explanation_text(
                          &g_fx, &g_exp, g_text_b, sizeof(g_text_b), &req_b),
                      "%d");
    TEST_ASSERT_EQUAL(req_a, req_b, "%zu");
    TEST_ASSERT_EQUAL(0, memcmp(g_text_a, g_text_b, req_a + 1u), "%d");
    TEST_END();
}

static maelys_result_t init_ord_ruleset(maelys_datalog_ruleset_t *r) {
    memset(r, 0, sizeof(*r));
    maelys_result_t rc =
        maelys_datalog_ruleset_init(r, "p4c65.ord", "format", k_zero_sha, 1);
    if (rc != MAELYS_OK) return rc;
    rc = maelys_datalog_predicate_registry_add_domain(
        &r->registry, "parent", 2u, MAELYS_DATALOG_PRED_KIND_EDB);
    if (rc != MAELYS_OK) return rc;
    rc = maelys_datalog_predicate_registry_add_domain(
        &r->registry, "ancestor", 2u,
        MAELYS_DATALOG_PRED_KIND_IDB | MAELYS_DATALOG_PRED_KIND_QUERY);
    if (rc != MAELYS_OK) return rc;
    rc = maelys_datalog_predicate_registry_freeze(&r->registry);
    if (rc != MAELYS_OK) return rc;
    static const char src[] =
        "ancestor(X, Y) :- parent(X, Y).\n"
        "ancestor(X, Z) :- parent(X, Y), ancestor(Y, Z).\n";
    return maelys_datalog_parse_ruleset(r, src, sizeof(src) - 1u);
}

static maelys_result_t add_pair(maelys_datalog_ruleset_t *r,
                                maelys_datalog_edb_t *edb,
                                const char *predicate,
                                const char *a,
                                const char *b) {
    maelys_datalog_term_t terms[2];
    maelys_datalog_symbol_id_t id_a = 0;
    maelys_datalog_symbol_id_t id_b = 0;
    maelys_result_t rc =
        maelys_datalog_symbol_intern(&r->symbols, a, strlen(a), &id_a);
    if (rc != MAELYS_OK) return rc;
    rc = maelys_datalog_symbol_intern(&r->symbols, b, strlen(b), &id_b);
    if (rc != MAELYS_OK) return rc;
    terms[0] = t_sym(id_a);
    terms[1] = t_sym(id_b);
    return maelys_datalog_edb_add_fact(edb, predicate, terms, 2u);
}

/* Ordered production traversal vs reference traversal: same TEXT bytes. */
static int test_fmt_ordered_vs_reference_traversal_same_text(void) {
    TEST_BEGIN();
    TEST_ASSERT_EQUAL(MAELYS_OK, init_ord_ruleset(&g_ord_r1), "%d");
    TEST_ASSERT_EQUAL(MAELYS_OK, init_ord_ruleset(&g_ord_r2), "%d");
    maelys_datalog_fact_t f1_store[8];
    maelys_datalog_fact_t f2_store[8];
    maelys_datalog_edb_t e1;
    maelys_datalog_edb_t e2;
    TEST_ASSERT_EQUAL(MAELYS_OK,
                      maelys_datalog_edb_init(&e1, f1_store, 8u, &g_ord_r1.symbols,
                                              &g_ord_r1.registry),
                      "%d");
    TEST_ASSERT_EQUAL(MAELYS_OK,
                      maelys_datalog_edb_init(&e2, f2_store, 8u, &g_ord_r2.symbols,
                                              &g_ord_r2.registry),
                      "%d");
    TEST_ASSERT_EQUAL(MAELYS_OK, add_pair(&g_ord_r1, &e1, "parent", "alice", "bob"), "%d");
    TEST_ASSERT_EQUAL(MAELYS_OK, add_pair(&g_ord_r1, &e1, "parent", "bob", "carol"), "%d");
    TEST_ASSERT_EQUAL(MAELYS_OK, add_pair(&g_ord_r2, &e2, "parent", "alice", "bob"), "%d");
    TEST_ASSERT_EQUAL(MAELYS_OK, add_pair(&g_ord_r2, &e2, "parent", "bob", "carol"), "%d");
    TEST_ASSERT_EQUAL(MAELYS_OK, maelys_datalog_edb_finalize(&e1), "%d");
    TEST_ASSERT_EQUAL(MAELYS_OK, maelys_datalog_edb_finalize(&e2), "%d");

    maelys_datalog_solve_result_t *ordered = NULL;
    maelys_datalog_solve_result_t *reference = NULL;
    TEST_ASSERT_EQUAL(MAELYS_OK, maelys_datalog_solve_once(&g_ord_r1, &e1, &ordered), "%d");
    TEST_ASSERT_EQUAL(MAELYS_OK,
                      maelys_datalog_test_solve_once_legacy_order(&g_ord_r2, &e2,
                                                                  &reference, NULL),
                      "%d");

    maelys_datalog_fact_t target1;
    maelys_datalog_fact_t target2;
    memset(&target1, 0, sizeof(target1));
    memset(&target2, 0, sizeof(target2));
    maelys_datalog_symbol_id_t a1 = 0;
    maelys_datalog_symbol_id_t c1 = 0;
    maelys_datalog_symbol_id_t a2 = 0;
    maelys_datalog_symbol_id_t c2 = 0;
    TEST_ASSERT_EQUAL(MAELYS_OK,
                      maelys_datalog_symbol_intern(&g_ord_r1.symbols, "alice", 5u, &a1),
                      "%d");
    TEST_ASSERT_EQUAL(MAELYS_OK,
                      maelys_datalog_symbol_intern(&g_ord_r1.symbols, "carol", 5u, &c1),
                      "%d");
    TEST_ASSERT_EQUAL(MAELYS_OK,
                      maelys_datalog_symbol_intern(&g_ord_r2.symbols, "alice", 5u, &a2),
                      "%d");
    TEST_ASSERT_EQUAL(MAELYS_OK,
                      maelys_datalog_symbol_intern(&g_ord_r2.symbols, "carol", 5u, &c2),
                      "%d");
    TEST_ASSERT_TRUE(maelys_datalog_predicate_registry_find(&g_ord_r1.registry,
                                                            "ancestor", 2u,
                                                            &target1.predicate_id));
    TEST_ASSERT_TRUE(maelys_datalog_predicate_registry_find(&g_ord_r2.registry,
                                                            "ancestor", 2u,
                                                            &target2.predicate_id));
    target1.arity = 2u;
    target1.terms[0] = t_sym(a1);
    target1.terms[1] = t_sym(c1);
    target2.arity = 2u;
    target2.terms[0] = t_sym(a2);
    target2.terms[1] = t_sym(c2);

    TEST_ASSERT_EQUAL(MAELYS_OK,
                      maelys_datalog_explain_solved_fact(ordered, &target1, &g_exp), "%d");
    TEST_ASSERT_EQUAL(MAELYS_OK,
                      maelys_datalog_explain_solved_fact(reference, &target2, &g_exp_b),
                      "%d");
    TEST_ASSERT_EQUAL((uint8_t)1u, g_exp.found, "%u");
    TEST_ASSERT_EQUAL((uint8_t)0u, g_exp.truncated, "%u");

    size_t req_a = 0u;
    size_t req_b = 0u;
    TEST_ASSERT_EQUAL(MAELYS_OK,
                      maelys_datalog_format_explanation_text(
                          &g_ord_r1, &g_exp, g_text_a, sizeof(g_text_a), &req_a),
                      "%d");
    TEST_ASSERT_EQUAL(MAELYS_OK,
                      maelys_datalog_format_explanation_text(
                          &g_ord_r2, &g_exp_b, g_text_b, sizeof(g_text_b), &req_b),
                      "%d");
    TEST_ASSERT_EQUAL(req_a, req_b, "%zu");
    TEST_ASSERT_EQUAL(0, memcmp(g_text_a, g_text_b, req_a + 1u), "%d");
    TEST_ASSERT_TRUE(text_wellformed(g_text_a, req_a));

    maelys_datalog_solve_result_free(ordered);
    maelys_datalog_solve_result_free(reference);
    maelys_datalog_ruleset_clear(&g_ord_r1);
    maelys_datalog_ruleset_clear(&g_ord_r2);
    TEST_END();
}

int main(int argc, char **argv) {
    test_case_t cases[] = {
        {"maelys_datalog_explanation_format/golden_not_derived",
         TEST_MODE_NON_BLOCKING, test_fmt_golden_not_derived},
        {"maelys_datalog_explanation_format/golden_truncated",
         TEST_MODE_NON_BLOCKING, test_fmt_golden_truncated},
        {"maelys_datalog_explanation_format/golden_normative_example",
         TEST_MODE_NON_BLOCKING, test_fmt_golden_normative_example},
        {"maelys_datalog_explanation_format/golden_policy_fact_origin",
         TEST_MODE_NON_BLOCKING, test_fmt_golden_policy_fact_origin},
        {"maelys_datalog_explanation_format/golden_two_distinct_idb_parents",
         TEST_MODE_NON_BLOCKING, test_fmt_golden_two_distinct_idb_parents},
        {"maelys_datalog_explanation_format/golden_shared_idb_parent",
         TEST_MODE_NON_BLOCKING, test_fmt_golden_shared_idb_parent},
        {"maelys_datalog_explanation_format/golden_comparison_six_ops",
         TEST_MODE_NON_BLOCKING, test_fmt_golden_comparison_six_ops},
        {"maelys_datalog_explanation_format/golden_int_extremes",
         TEST_MODE_NON_BLOCKING, test_fmt_golden_int_extremes},
        {"maelys_datalog_explanation_format/golden_bool",
         TEST_MODE_NON_BLOCKING, test_fmt_golden_bool},
        {"maelys_datalog_explanation_format/golden_zero_arity",
         TEST_MODE_NON_BLOCKING, test_fmt_golden_zero_arity},
        {"maelys_datalog_explanation_format/golden_ascii_escapes",
         TEST_MODE_NON_BLOCKING, test_fmt_golden_ascii_escapes},
        {"maelys_datalog_explanation_format/golden_utf8_non_ascii",
         TEST_MODE_NON_BLOCKING, test_fmt_golden_utf8_non_ascii},
        {"maelys_datalog_explanation_format/golden_invalid_utf8",
         TEST_MODE_NON_BLOCKING, test_fmt_golden_invalid_utf8},
        {"maelys_datalog_explanation_format/golden_max_lengths",
         TEST_MODE_NON_BLOCKING, test_fmt_golden_max_lengths},
        {"maelys_datalog_explanation_format/all_lines_hygiene",
         TEST_MODE_NON_BLOCKING, test_fmt_all_lines_hygiene},
        {"maelys_datalog_explanation_format/count_only_exact",
         TEST_MODE_NON_BLOCKING, test_fmt_count_only_exact},
        {"maelys_datalog_explanation_format/capacity_required_plus_one_succeeds",
         TEST_MODE_NON_BLOCKING, test_fmt_capacity_required_plus_one_succeeds},
        {"maelys_datalog_explanation_format/capacity_required_fails_atomic",
         TEST_MODE_NON_BLOCKING, test_fmt_capacity_required_fails_atomic},
        {"maelys_datalog_explanation_format/insufficient_capacities_never_prefix",
         TEST_MODE_NON_BLOCKING, test_fmt_insufficient_capacities_never_prefix},
        {"maelys_datalog_explanation_format/two_sufficient_sizes_identical",
         TEST_MODE_NON_BLOCKING, test_fmt_two_sufficient_sizes_identical},
        {"maelys_datalog_explanation_format/contract_null_arguments",
         TEST_MODE_NON_BLOCKING, test_fmt_contract_null_arguments},
        {"maelys_datalog_explanation_format/invalid_state_ruleset",
         TEST_MODE_NON_BLOCKING, test_fmt_invalid_state_ruleset},
        {"maelys_datalog_explanation_format/invalid_found_truncated_matrix",
         TEST_MODE_NON_BLOCKING, test_fmt_invalid_found_truncated_matrix},
        {"maelys_datalog_explanation_format/invalid_ranges_and_body_indices",
         TEST_MODE_NON_BLOCKING, test_fmt_invalid_ranges_and_body_indices},
        {"maelys_datalog_explanation_format/invalid_rule_ids_and_vocabulary",
         TEST_MODE_NON_BLOCKING, test_fmt_invalid_rule_ids_and_vocabulary},
        {"maelys_datalog_explanation_format/invalid_terms",
         TEST_MODE_NON_BLOCKING, test_fmt_invalid_terms},
        {"maelys_datalog_explanation_format/invalid_premises_and_links",
         TEST_MODE_NON_BLOCKING, test_fmt_invalid_premises_and_links},
        {"maelys_datalog_explanation_format/non_mutation_bitwise",
         TEST_MODE_NON_BLOCKING, test_fmt_non_mutation_bitwise},
        {"maelys_datalog_explanation_format/two_calls_identical",
         TEST_MODE_NON_BLOCKING, test_fmt_two_calls_identical},
        {"maelys_datalog_explanation_format/ordered_vs_reference_traversal_same_text",
         TEST_MODE_NON_BLOCKING, test_fmt_ordered_vs_reference_traversal_same_text},
    };
    return test_main("maelys_datalog_explanation_format",
                     cases,
                     (int)(sizeof(cases) / sizeof(cases[0])),
                     argc,
                     argv);
}
