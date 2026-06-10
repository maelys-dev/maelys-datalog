#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <string.h>
#include <errno.h>
#include <sys/types.h>
#include <sys/stat.h>   /* mkdir */
#include <unistd.h>     /* isatty, fileno */
#include "test_framework.h"
#include "tests/helpers/test_log.h"
#include <locale.h>

TestRunnerState g_test_state = {0};

/* === Couleurs ============================================================= */
static int s_color_on = 0; /* activé selon test_color_enabled() */

int test_color_enabled(void) {
    const char *env = getenv("TEST_COLOR");
    if (env && *env == '0') return 0;      // TEST_COLOR=0 -> force off
    return isatty(fileno(stderr));          // sinon: on si TTY
}

/* Fonctions exposées au .h (utilisées par les macros TEST_NOTE_*) */
const char* test_color_reset(void)  { return s_color_on ? "\x1b[0m"  : ""; }
const char* test_color_dim(void)    { return s_color_on ? "\x1b[2m"  : ""; }
const char* test_color_green(void)  { return s_color_on ? "\x1b[32m" : ""; }
const char* test_color_yellow(void) { return s_color_on ? "\x1b[33m" : ""; }
const char* test_color_red(void)    { return s_color_on ? "\x1b[31m" : ""; }
const char* test_color_blue(void)   { return s_color_on ? "\x1b[34m" : ""; }
const char* test_color_cyan(void)   { return s_color_on ? "\x1b[36m" : ""; }

/* === Sink des logs (pour mode silencieux et/ou log de suite) ============== */
static FILE *s_log_fp_current = NULL;  /* sink actuel (stderr par défaut, ou fichier de suite) */
static FILE *s_log_fp_null    = NULL;  /* /dev/null pour museler tous les logs */

static void quiet_push(void) {
    /* Active le silence console UNIQUEMENT si on n’écrit pas déjà dans un fichier de suite */
    if (g_test_state.suite_logs) return; /* on laisse les logs aller au fichier */
    if (!s_log_fp_null) {
        s_log_fp_null = fopen("/dev/null", "w");
        if (!s_log_fp_null) s_log_fp_null = stderr; /* fallback parano */
    }
    test_log_set_file(s_log_fp_null);
}

static void quiet_pop(void) {
    if (s_log_fp_current) test_log_set_file(s_log_fp_current);
}

static const char* mode_str(int mode) {
    return mode == TEST_MODE_BLOCKING ? "blocking" : "non-block";
}

/* mkdir -p like (simple & portable) */
static int ensure_dir(const char *path) {
    if (!path || !*path) return 0;
    char buf[512];
    snprintf(buf, sizeof(buf), "%s", path);
    size_t len = strlen(buf);
    if (len == 0) return 0;
    if (buf[len-1] == '/') buf[len-1] = '\0';

    for (char *p = buf + 1; *p; ++p) {
        if (*p == '/') { *p = '\0'; if (mkdir(buf, 0777) != 0 && errno != EEXIST) return -1; *p = '/'; }
    }
    if (mkdir(buf, 0777) != 0 && errno != EEXIST) return -1;
    return 0;
}

/* ================== Alignement : largeurs calculées pour la suite ========= */
static int L_PROGRESS = 0;  /* largeur pour "[i/n] " */
static int L_NAME     = 0;  /* largeur pour le nom (clippé si trop long) */
static int L_MODE     = 0;  /* largeur pour " (blocking|non-block)" */

static int default_name_col_width(void) {
    const char *env = getenv("TEST_NAME_COL");
    int w = (env && *env) ? atoi(env) : 36;
    if (w < 16) w = 16;
    if (w > 80) w = 80;
    return w;
}

static void fprint_name_clipped(FILE *out, const char *name, int width) {
    if (!name) name = "";
    int len = (int)strlen(name);
    if (len <= width) {
        fprintf(out, "%-*s", width, name);
        return;
    }
    if (width <= 3) {
        for (int i = 0; i < width; ++i) fputc('.', out);
        return;
    }
    fwrite(name, 1, (size_t)(width - 3), out);
    fputs("...", out);
}

static void compute_layout_for_suite_count_and_names(int total, int max_name_len) {
    char tmp[32];
    snprintf(tmp, sizeof(tmp), "[%d/%d] ", total, total);
    L_PROGRESS = (int)strlen(tmp);

    int cap = default_name_col_width();
    L_NAME = (max_name_len > cap) ? cap : max_name_len;

    int m1 = 3 + (int)strlen("blocking");  /* " (blocking)"   */
    int m2 = 3 + (int)strlen("non-block"); /* " (non-block)"  */
    L_MODE = (m2 > m1) ? m2 : m1;          /* généralement 12 */
}

/* ================== Bannière avec padding & wrap ========================== */
#ifndef TEST_BANNER_INNER
#define TEST_BANNER_INNER 71  /* largeur interne entre les barres ┃ ┃ */
#endif

static void box_print_border_top(int inner) {
    fputs("┏", stdout);
    for (int i = 0; i < inner; ++i) fputs("━", stdout);
    fputs("┓", stdout);
    fputc('\n', stdout);
}

static void box_print_border_bottom(int inner) {
    fputs("┗", stdout);
    for (int i = 0; i < inner; ++i) fputs("━", stdout);
    fputs("┛", stdout);
    fputc('\n', stdout);
}

/* Imprime une ligne à l’intérieur, avec 1 espace de marge de chaque côté. */
static void box_print_line_segment(const char *s, int len, int inner) {
    const int content_w = (inner > 2) ? inner - 2 : inner;

    fputs("┃", stdout);
    if (inner <= 0) { fputs("┃", stdout); fputc('\n', stdout); return; }

    fputc(' ', stdout); /* marge gauche */

    int to_print = len;
    if (to_print > content_w) to_print = content_w;
    if (to_print > 0) fwrite(s, 1, (size_t)to_print, stdout);

    int pad = content_w - to_print;
    for (int i = 0; i < pad; ++i) fputc(' ', stdout);

    fputc(' ', stdout); /* marge droite */
    fputs("┃", stdout);
    fputc('\n', stdout);
}

/* Wrap simple : coupe sur espace si possible, sinon coupe dur à content_w. */
static void box_print_wrapped(const char *msg, int inner) {
    const int content_w = (inner > 2) ? inner - 2 : inner;
    const char *p = msg ? msg : "";
    if (*p == '\0') { box_print_line_segment("", 0, inner); return; }

    while (*p) {
        int n = 0, last_space = -1;
        while (p[n] && n < content_w) {
            if (p[n] == ' ') last_space = n;
            n++;
        }
        int take = n;
        if (p[take] && last_space > 0) take = last_space; /* wrap propre */
        box_print_line_segment(p, take, inner);
        p += take;
        while (*p == ' ') ++p; /* skip espaces en début de ligne suivante */
    }
}

static void box_print_message(const char *msg) {
    const int inner = TEST_BANNER_INNER;
    box_print_border_top(inner);
    box_print_wrapped(msg ? msg : "", inner);
    box_print_border_bottom(inner);
}

/* ------------- Exécution d’un test ---------------------------------------- */
static int run_single_test_case(const test_case_t *test_case, int index, int total) {
    /* 1) Exécuter le test — logs AVANT la synthèse. Si TEST_VERBOSE=0 → silence console (sauf suite log). */
    if (!g_test_state.verbosity_level) quiet_push();
    int rc = test_case->fn();
    if (!g_test_state.verbosity_level) quiet_pop();
    fflush(stderr);

    /* 2) Ligne récap alignée (colonnes fixes) */
    char progress[32];
    snprintf(progress, sizeof(progress), "[%d/%d] ", index + 1, total);

    fprintf(stdout, "%-*s", L_PROGRESS, progress);
    fprint_name_clipped(stdout, test_case->name, L_NAME);

    const char *m = mode_str(test_case->mode);
    int printed_mode = fprintf(stdout, " (%s)", m);
    int pad = (L_MODE - printed_mode) + 2;
    if (pad < 1) pad = 1;
    while (pad--) fputc(' ', stdout);

    if (rc == 0) {
        fprintf(stdout, "✅ OK\n");
    } else {
        g_test_state.tests_failed++;
        fprintf(stdout, "❌ FAILED\n");
    }
    fflush(stdout);
    return rc;
}

/* ------------- Runners ---------------------------------------------------- */
int run_suite_all(const test_case_t *cases, int n) {
    g_test_state.tests_run = n;
    g_test_state.tests_failed = 0;
    g_test_state.max_failures = n;

    int max_name = 0;
    for (int i = 0; i < n; ++i) {
        int L = (int)strlen(cases[i].name);
        if (L > max_name) max_name = L;
    }
    compute_layout_for_suite_count_and_names(n, max_name);

    char title[256];
    snprintf(title, sizeof(title), "Running all %d tests", n);
    box_print_message(title);

    for (int i = 0; i < n; i++) {
        int rc = run_single_test_case(&cases[i], i, n);
        if (cases[i].mode == TEST_MODE_BLOCKING && rc != 0) break;
    }

    printf("\nSummary: %d/%d failed %s\n",
           g_test_state.tests_failed, g_test_state.tests_run,
           g_test_state.tests_failed == 0 ? "✅" : "❌");
    return g_test_state.tests_failed;
}

int run_suite_filtered(const test_case_t *all_cases, int n_total, const char *filter_str) {
    if (n_total <= 0) return 0;

    const test_case_t **filtered = (const test_case_t **)malloc(sizeof(*filtered) * n_total);
    if (!filtered) { fprintf(stderr, "Allocation failed for filtered list\n"); return 1; }

    int n = 0;
    for (int i = 0; i < n_total; i++) {
        if (!filter_str || !*filter_str || strstr(all_cases[i].name, filter_str)) {
            filtered[n++] = &all_cases[i];
        }
    }

    if (n == 0) {
        printf("No tests match filter '%s'.\n", filter_str ? filter_str : "");
        free(filtered);
        return 0;
    }

    g_test_state.tests_run = n;
    g_test_state.tests_failed = 0;
    g_test_state.max_failures = n;

    int max_name = 0;
    for (int i = 0; i < n; ++i) {
        int L = (int)strlen(filtered[i]->name);
        if (L > max_name) max_name = L;
    }
    compute_layout_for_suite_count_and_names(n, max_name);

    char title[512];
    snprintf(title, sizeof(title),
             "Running %d filtered test case%s (filter: '%s')",
             n, n > 1 ? "s" : "", filter_str ? filter_str : "");
    box_print_message(title);

    for (int i = 0; i < n; i++) {
        int rc = run_single_test_case(filtered[i], i, n);
        if (filtered[i]->mode == TEST_MODE_BLOCKING && rc != 0) break;
    }

    printf("\nSummary: %d/%d failed %s\n",
           g_test_state.tests_failed, g_test_state.tests_run,
           g_test_state.tests_failed == 0 ? "✅" : "❌");

    free(filtered);
    return g_test_state.tests_failed;
}

/* ------------- Logging init ------------------------------------------------ */
void tests_init_logging(void) {
    const char *verbose_str = getenv("TEST_VERBOSE");
    g_test_state.verbosity_level = (verbose_str && atoi(verbose_str) > 0);
    g_test_state.default_log_level = test_log_get_level();

    const char *lvl = getenv("TEST_LOG_LEVEL");
    if (lvl) {
        if      (strcmp(lvl, "TRACE") == 0) g_test_state.default_log_level = TEST_LOG_LEVEL_TRACE;
        else if (strcmp(lvl, "DEBUG") == 0) g_test_state.default_log_level = TEST_LOG_LEVEL_DEBUG;
        else if (strcmp(lvl, "INFO")  == 0) g_test_state.default_log_level = TEST_LOG_LEVEL_INFO;
        else if (strcmp(lvl, "WARN")  == 0) g_test_state.default_log_level = TEST_LOG_LEVEL_WARN;
        else if (strcmp(lvl, "ERROR") == 0) g_test_state.default_log_level = TEST_LOG_LEVEL_ERROR;
    }

    const char *suite_log_env = getenv("TEST_SUITE_LOG");
    g_test_state.suite_logs = (suite_log_env && atoi(suite_log_env) > 0);

    snprintf(g_test_state.log_dir, sizeof(g_test_state.log_dir), "%s", "build/test-logs");
    const char *dir_env = getenv("TEST_LOG_DIR");
    if (dir_env && *dir_env) snprintf(g_test_state.log_dir, sizeof(g_test_state.log_dir), "%s", dir_env);

    test_log_init(g_test_state.default_log_level, stderr, 1);
    test_log_set_file(stderr);
    s_log_fp_current = stderr;

    /* Init couleurs */
    s_color_on = test_color_enabled();

    /* +++ initialiser la locale pour sorties UTF-8 +++ */
    setlocale(LC_ALL, "");
}

void test_detailf(const char *fmt, ...) {
    if (g_test_state.verbosity_level > 0) {
        va_list args;
        va_start(args, fmt);
        vfprintf(stderr, fmt, args);
        va_end(args);
        if (!fmt || fmt[0] == '\0' || fmt[strlen(fmt)-1] != '\n') fputc('\n', stderr);
        fflush(stderr);
    }
}

/* ------------- Aide & main générique -------------------------------------- */
void test_print_usage(const char *prog) {
    fprintf(stdout,
        "Usage: %s [options] [FILTER]\n"
        "Options:\n"
        "  -k, --filter STR   Filtre (sous-chaine) des noms de tests\n"
        "  -v                 Verbose (TEST_VERBOSE=1)\n"
        "  --suite-log        Ecrit les logs de la suite dans <LOG_DIR>/<suite>.log\n"
        "  --log-dir DIR      Dossier des logs de suite (defaut: build/test-logs)\n"
        "  --list             Liste les tests et quitte\n"
        "  -h, --help         Affiche cette aide\n"
        "Notes:\n"
        "  Si aucun filtre n'est fourni, on utilise la variable d'environnement FILTER.\n",
        prog ? prog : "tests");
}

int test_main(const char *suite_name, const test_case_t *cases, int n, int argc, char **argv) {
    const char *prog = (argc > 0 && argv && argv[0]) ? argv[0] : "tests";
    const char *filter = getenv("FILTER");
    int want_list = 0;
    int want_suite_log_cli = 0;
    const char *log_dir_cli = NULL;

    tests_init_logging();

    for (int i = 1; i < argc; ++i) {
        const char *a = argv[i];
        if (!a) continue;

        if (strcmp(a, "-k") == 0 || strcmp(a, "--filter") == 0) {
            if (i+1 < argc) filter = argv[++i];
            else { fprintf(stderr, "Option %s requires STR\n", a); test_print_usage(prog); return 2; }
        } else if (strcmp(a, "-v") == 0) {
            g_test_state.verbosity_level = 1;
        } else if (strcmp(a, "--suite-log") == 0) {
            want_suite_log_cli = 1;
        } else if (strcmp(a, "--log-dir") == 0) {
            if (i+1 < argc) log_dir_cli = argv[++i];
            else { fprintf(stderr, "Option %s requires DIR\n", a); test_print_usage(prog); return 2; }
        } else if (strcmp(a, "--list") == 0) {
            want_list = 1;
        } else if (strcmp(a, "-h") == 0 || strcmp(a, "--help") == 0) {
            test_print_usage(prog);
            return 0;
        } else {
            if (!filter || !*filter) filter = a;
        }
    }

    if (log_dir_cli && *log_dir_cli) snprintf(g_test_state.log_dir, sizeof(g_test_state.log_dir), "%s", log_dir_cli);
    if (want_suite_log_cli) g_test_state.suite_logs = 1;

    if (g_test_state.suite_logs && suite_name && *suite_name) {
        if (ensure_dir(g_test_state.log_dir) == 0) {
            char path[512];
            snprintf(path, sizeof(path), "%s/%s.log", g_test_state.log_dir, suite_name);
            FILE *f = fopen(path, "w");
            if (f) {
                test_log_set_file(f);
                s_log_fp_current = f;
            } else {
                fprintf(stderr, "Cannot open suite log '%s' (%s)\n", path, strerror(errno));
            }
        } else {
            fprintf(stderr, "Cannot create log dir '%s' (%s)\n", g_test_state.log_dir, strerror(errno));
        }
    }

    if (want_list) {
        for (int i = 0; i < n; ++i) puts(cases[i].name);
        return 0;
    }

    int rc = (filter && *filter) ? run_suite_filtered(cases, n, filter)
                                 : run_suite_all(cases, n);
    return rc ? 1 : 0;
}
