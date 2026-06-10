#pragma once
#ifndef TEST_FRAMEWORK_H
#define TEST_FRAMEWORK_H

#include <stddef.h>
#include <string.h>   /* strcmp dans les macros */
#include "tests/helpers/test_log.h"

/* ===================== Types & état global ===================== */

typedef int (*test_fn_t)(void);

#define TEST_MODE_NON_BLOCKING 0
#define TEST_MODE_BLOCKING     1

typedef struct {
    const char *name;
    int         mode;   /* TEST_MODE_* */
    test_fn_t   fn;
} test_case_t;

typedef struct {
    int tests_run;
    int tests_failed;
    int max_failures;
    char **failed_test_names;
    int verbosity_level;            /* TEST_VERBOSE>0 */
    test_log_level_t default_log_level;

    int  suite_logs;                /* 1 = écrire les logs dans un fichier de suite */
    char log_dir[256];              /* dossier pour logs, défaut "build/test-logs" */
} TestRunnerState;

extern TestRunnerState g_test_state;

/* ===================== API runner ===================== */

int run_suite_all(const test_case_t *cases, int n);
int run_suite_filtered(const test_case_t *all_cases, int n_total, const char *filter_str);

/* Initialisation du logging (lit TEST_VERBOSE / TEST_LOG_LEVEL / TEST_SUITE_LOG / TEST_LOG_DIR) */
void tests_init_logging(void);

/* Impression d’aide CLI, utilisée par test_main */
void test_print_usage(const char *prog);

/* Main générique pour toutes les familles de tests.
 * - suite_name: nom court de la famille (sert au nom du fichier log de suite)
 * - cases/n   : tableau de cas de test
 * - argc/argv : arguments CLI
 * Retourne 0 si succès, 1 si échec(s).
 */
int test_main(const char *suite_name, const test_case_t *cases, int n, int argc, char **argv);

/* Petit helper “détail” (sort sur stderr). Conservé pour compat. */
void test_detailf(const char *fmt, ...);

/* Indique si on doit émettre des couleurs ANSI (implémentée dans test_framework.c) */
int test_color_enabled(void);

/* ===================== Couleurs ANSI (auto-TTY) ===================== */

#define TEST_COLOR_RESET   (test_color_enabled() ? "\x1b[0m"  : "")
#define TEST_COLOR_DIM     (test_color_enabled() ? "\x1b[2m"  : "")
#define TEST_COLOR_BOLD    (test_color_enabled() ? "\x1b[1m"  : "")
#define TEST_COLOR_GREEN   (test_color_enabled() ? "\x1b[32m" : "")
#define TEST_COLOR_YELLOW  (test_color_enabled() ? "\x1b[33m" : "")
#define TEST_COLOR_RED     (test_color_enabled() ? "\x1b[31m" : "")
#define TEST_COLOR_CYAN    (test_color_enabled() ? "\x1b[36m" : "")

/* ===================== Macros “notes” lisibles ===================== */
/* Utiles à l’intérieur d’un test pour écrire des lignes explicatives. */

/* Helpers RAW (pas de couleur/style intégré) */
#define TEST_PUTS_RAW(fmt, ...)    do { if (g_test_state.verbosity_level > 0) \
    fprintf(stderr, fmt, ##__VA_ARGS__); } while (0)
#define TEST_PUTSLN_RAW(fmt, ...)  do { if (g_test_state.verbosity_level > 0) \
    fprintf(stderr, fmt "\n", ##__VA_ARGS__); } while (0)

/* ===== Notes colorées ===== */
#define TEST_NOTE(fmt, ...)     TEST_PUTS_RAW  ("%s" fmt "%s", TEST_COLOR_DIM,   ##__VA_ARGS__, TEST_COLOR_RESET)
#define TEST_NOTELN(fmt, ...)   TEST_PUTSLN_RAW("%s" fmt "%s", TEST_COLOR_DIM,   ##__VA_ARGS__, TEST_COLOR_RESET)

#define TEST_NOTE_OK(fmt, ...)  TEST_PUTSLN_RAW("%s" fmt "%s", TEST_COLOR_GREEN, ##__VA_ARGS__, TEST_COLOR_RESET)
#define TEST_NOTE_INFO(fmt, ...)TEST_PUTSLN_RAW("%s" fmt "%s", TEST_COLOR_CYAN,  ##__VA_ARGS__, TEST_COLOR_RESET)
#define TEST_NOTE_WARN(fmt, ...)TEST_PUTSLN_RAW("%s" fmt "%s", TEST_COLOR_YELLOW,##__VA_ARGS__, TEST_COLOR_RESET)
#define TEST_NOTE_ERR(fmt, ...) TEST_PUTSLN_RAW("%s" fmt "%s", TEST_COLOR_RED,   ##__VA_ARGS__, TEST_COLOR_RESET)

/* ========= Messages "purs" (pas de format printf) =========
   -> À utiliser quand le texte peut contenir '%' ou emojis.
   -> Respecte TEST_VERBOSE (n'affiche que si > 0). */

/* MSG (chaînes simples) parfaitement colorés */
#define TEST_MSG(msg)         TEST_PUTS_RAW("%s%s%s",         TEST_COLOR_DIM,   (msg), TEST_COLOR_RESET)
#define TEST_MSGLN(msg)       TEST_PUTSLN_RAW("%s%s%s",       TEST_COLOR_DIM,   (msg), TEST_COLOR_RESET)
#define TEST_MSG_OK(msg)      TEST_PUTSLN_RAW("%s%s%s",       TEST_COLOR_GREEN, (msg), TEST_COLOR_RESET)
#define TEST_MSG_INFO(msg)    TEST_PUTSLN_RAW("%s%s%s",       TEST_COLOR_CYAN,  (msg), TEST_COLOR_RESET)
#define TEST_MSG_WARN(msg)    TEST_PUTSLN_RAW("%s%s%s",       TEST_COLOR_YELLOW,(msg), TEST_COLOR_RESET)
#define TEST_MSG_ERR(msg)     TEST_PUTSLN_RAW("%s%s%s",       TEST_COLOR_RED,   (msg), TEST_COLOR_RESET)

/* ===================== Macros d’assertion ===================== */

#define TEST_BEGIN()                                   int rc = 0;
#define TEST_END()                                     return rc;
#define TEST_FAIL()                                    rc = 1;
#define TEST_FAIL_FMT(fmt, ...)                        do { TEST_NOTE_ERR(fmt, ##__VA_ARGS__); TEST_FAIL(); } while (0)
#define TEST_ASSERT_TRUE(cond)                         if (!(cond)) { TEST_FAIL_FMT("assert_true failed: " #cond); }
#define TEST_ASSERT_FALSE(cond)                        if ((cond))  { TEST_FAIL_FMT("assert_false failed: " #cond); }
#define TEST_ASSERT_EQUAL(expected, got, fmt)          if ((expected) != (got)) { TEST_FAIL_FMT("assert_equal failed. Expected: " fmt ", Got: " fmt, (expected), (got)); }
#define TEST_ASSERT_NOT_NULL(ptr)                      if ((ptr) == NULL) { TEST_FAIL_FMT("assert_not_null failed: " #ptr); }
#define TEST_ASSERT_NULL(ptr)                          if ((ptr) != NULL) { TEST_FAIL_FMT("assert_null failed: " #ptr); }
#define TEST_ASSERT_EQUAL_STRING(exp, got)             if (strcmp((exp), (got)) != 0) { TEST_FAIL_FMT("assert_equal_string failed. Expected: \"%s\", Got: \"%s\"", (exp), (got)); }

#endif /* TEST_FRAMEWORK_H */
