#include "helpers/test_framework.h"

#include "common/maelys_sha256.h"
#include "src/core/maelys_datalog_diagnostic.h"
#include "src/core/maelys_datalog_domain_registry.h"
#include "src/core/maelys_datalog_parser.h"
#include "src/core/maelys_datalog_predicate_registry.h"
#include "src/core/maelys_datalog_ruleset.h"
#include "src/core/maelys_datalog_solver.h"
#include "src/manifest/maelys_datalog_manifest.h"

#include <dirent.h>
#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#define CORPUS_ROOT "tests/corpus"
#define MAX_CORPUS_FILES 256u
#define MAX_CORPUS_PATH 512u

typedef enum {
    CORPUS_EXPECT_OK = 0,
    CORPUS_EXPECT_FAIL,
    CORPUS_EXPECT_DIAG
} corpus_expect_kind_t;

typedef struct {
    corpus_expect_kind_t kind;
    maelys_datalog_diag_code_t code;
    char token[64];
} corpus_expect_t;

typedef struct {
    char paths[MAX_CORPUS_FILES][MAX_CORPUS_PATH];
    size_t count;
} corpus_file_list_t;

static const char k_zero_hash[65] =
    "0000000000000000000000000000000000000000000000000000000000000000";

static const char k_conflict_domain[] = "corpus_registry_conflict";
static const char k_conflict_policy_id[] = "corpus_registry_conflict_policy";
static const maelys_datalog_predicate_def_t k_conflict_domain_table[] = {
    {.name = "safe", .arity = 1, .kind_flags = MAELYS_DATALOG_PRED_KIND_EDB},
    {.name = "allow",
     .arity = 1,
     .kind_flags = MAELYS_DATALOG_PRED_KIND_IDB | MAELYS_DATALOG_PRED_KIND_QUERY},
};

static int add_predicate(maelys_datalog_ruleset_t *ruleset,
                         const char *name,
                         uint8_t arity,
                         uint8_t kind)
{
    return maelys_datalog_predicate_registry_add_domain(
        &ruleset->registry, name, arity, kind);
}

static int add_atom(maelys_datalog_ruleset_t *ruleset,
                    const char *atom)
{
    return maelys_datalog_predicate_registry_add_atom(
        &ruleset->registry, atom);
}

static int init_corpus_ruleset(maelys_datalog_ruleset_t *ruleset)
{
    int rc = maelys_datalog_ruleset_init(
        ruleset, "corpus.policy", "corpus", k_zero_hash, 1);
    if (rc != MAELYS_OK)
        return rc;

    struct {
        const char *name;
        uint8_t arity;
        uint8_t kind;
    } preds[] = {
        {"user", 1, MAELYS_DATALOG_PRED_KIND_EDB},
        {"blocked", 1, MAELYS_DATALOG_PRED_KIND_POLICY_FACT},
        {"safe", 1, MAELYS_DATALOG_PRED_KIND_POLICY_FACT},
        {"allow", 1,
         MAELYS_DATALOG_PRED_KIND_IDB | MAELYS_DATALOG_PRED_KIND_QUERY},
        {"deny", 2,
         MAELYS_DATALOG_PRED_KIND_IDB | MAELYS_DATALOG_PRED_KIND_QUERY},
        {"deny_any", 1, MAELYS_DATALOG_PRED_KIND_IDB},
        {"edge", 2, MAELYS_DATALOG_PRED_KIND_EDB},
        {"path", 2,
         MAELYS_DATALOG_PRED_KIND_IDB | MAELYS_DATALOG_PRED_KIND_QUERY},
        {"score", 2, MAELYS_DATALOG_PRED_KIND_EDB},
        {"owns", 2, MAELYS_DATALOG_PRED_KIND_EDB},
        {"valid_commit_msg", 1, MAELYS_DATALOG_PRED_KIND_EDB},
    };

    for (size_t i = 0; i < sizeof(preds) / sizeof(preds[0]); i++) {
        rc = add_predicate(
            ruleset, preds[i].name, preds[i].arity, preds[i].kind);
        if (rc != MAELYS_OK)
            return rc;
    }

    const char *atoms[] = {
        "alice",       "bob",      "mallory", "doc.pdf",
        "other.pdf",   "a",        "b",       "c",
        "cli_pivot",   "stdin",    "text",    "proj-1",
        "ok",          "msg",      "commit",  "blocked-user",
    };

    for (size_t a = 0; a < sizeof(atoms) / sizeof(atoms[0]); a++) {
        rc = add_atom(ruleset, atoms[a]);
        if (rc != MAELYS_OK)
            return rc;
    }

    maelys_datalog_predicate_registry_freeze(&ruleset->registry);
    return MAELYS_OK;
}

static int has_suffix(const char *s, const char *suffix)
{
    size_t slen = strlen(s);
    size_t tlen = strlen(suffix);
    return slen >= tlen && memcmp(s + slen - tlen, suffix, tlen) == 0;
}

static int read_file_binary(const char *path, uint8_t **out, size_t *out_len)
{
    FILE *fp = fopen(path, "rb");
    if (!fp)
        return MAELYS_ERR_INVALID_FIELD;
    if (fseek(fp, 0, SEEK_END) != 0) {
        fclose(fp);
        return MAELYS_ERR_INVALID_FIELD;
    }
    long n = ftell(fp);
    if (n < 0) {
        fclose(fp);
        return MAELYS_ERR_INVALID_FIELD;
    }
    if (fseek(fp, 0, SEEK_SET) != 0) {
        fclose(fp);
        return MAELYS_ERR_INVALID_FIELD;
    }

    uint8_t *buf = (uint8_t *)malloc((size_t)n + 1u);
    if (!buf) {
        fclose(fp);
        return MAELYS_ERR_INVALID_FIELD;
    }
    size_t got = fread(buf, 1, (size_t)n, fp);
    int read_error = ferror(fp);
    fclose(fp);
    if (read_error || got != (size_t)n) {
        free(buf);
        return MAELYS_ERR_INVALID_FIELD;
    }
    buf[got] = 0;
    *out = buf;
    *out_len = got;
    return MAELYS_OK;
}

static int buffer_is_ws_only(const uint8_t *buf, size_t len)
{
    for (size_t i = 0; i < len; i++) {
        if (buf[i] != ' ' && buf[i] != '\t' &&
            buf[i] != '\n' && buf[i] != '\r')
            return 0;
    }
    return 1;
}

static maelys_datalog_diag_code_t diag_from_token(const char *token)
{
    struct {
        const char *token;
        maelys_datalog_diag_code_t code;
    } map[] = {
        {"LEXER_INVALID_TOKEN", MAELYS_DATALOG_DIAG_LEXER_INVALID_TOKEN},
        {"LEXER_UNSUPPORTED_CONSTRUCT",
         MAELYS_DATALOG_DIAG_LEXER_UNSUPPORTED_CONSTRUCT},
        {"LEXER_INVALID_UTF8", MAELYS_DATALOG_DIAG_LEXER_INVALID_UTF8},
        {"LEXER_STRING_TOO_LONG",
         MAELYS_DATALOG_DIAG_LEXER_STRING_TOO_LONG},
        {"PARSER_EXPECTED_PREDICATE",
         MAELYS_DATALOG_DIAG_PARSER_EXPECTED_PREDICATE},
        {"PARSER_UNKNOWN_PREDICATE",
         MAELYS_DATALOG_DIAG_PARSER_UNKNOWN_PREDICATE},
        {"PARSER_ARITY_MISMATCH",
         MAELYS_DATALOG_DIAG_PARSER_ARITY_MISMATCH},
        {"PARSER_UNKNOWN_ATOM",
         MAELYS_DATALOG_DIAG_PARSER_UNKNOWN_ATOM},
        {"PARSER_RULE_HEAD_EDB_FORBIDDEN",
         MAELYS_DATALOG_DIAG_PARSER_RULE_HEAD_EDB_FORBIDDEN},
        {"PARSER_RULE_BODY_LITERAL_OVERFLOW",
         MAELYS_DATALOG_DIAG_PARSER_RULE_BODY_LITERAL_OVERFLOW},
        {"PARSER_UNSAFE_VARIABLE",
         MAELYS_DATALOG_DIAG_PARSER_UNSAFE_VARIABLE},
        {"PARSER_INVALID_COMPARISON",
         MAELYS_DATALOG_DIAG_PARSER_INVALID_COMPARISON},
        {"PARSER_EXPECTED_DOT",
         MAELYS_DATALOG_DIAG_PARSER_EXPECTED_DOT},
        {"PARSER_EXPECTED_NECK",
         MAELYS_DATALOG_DIAG_PARSER_EXPECTED_NECK},
        {"PARSER_FACT_USES_NON_BASE_PREDICATE",
         MAELYS_DATALOG_DIAG_PARSER_FACT_USES_NON_BASE_PREDICATE},
        {"PARSER_ANONYMOUS_VARIABLE_IN_HEAD",
         MAELYS_DATALOG_DIAG_PARSER_ANONYMOUS_VARIABLE_IN_HEAD},
        {"PARSER_ANONYMOUS_VARIABLE_IN_COMPARISON",
         MAELYS_DATALOG_DIAG_PARSER_ANONYMOUS_VARIABLE_IN_COMPARISON},
        {"PARSER_ANONYMOUS_VARIABLE_IN_FACT",
         MAELYS_DATALOG_DIAG_PARSER_ANONYMOUS_VARIABLE_IN_FACT},
        {"POLICY_NOT_STRATIFIABLE",
         MAELYS_DATALOG_DIAG_POLICY_NOT_STRATIFIABLE},
    };

    for (size_t i = 0; i < sizeof(map) / sizeof(map[0]); i++) {
        if (strcmp(token, map[i].token) == 0)
            return map[i].code;
    }
    return MAELYS_DATALOG_DIAG_NONE;
}

static int parse_expect(const uint8_t *buf,
                        size_t len,
                        corpus_expect_t *expect)
{
    memset(expect, 0, sizeof(*expect));
    if (len == 0 || buffer_is_ws_only(buf, len)) {
        expect->kind = CORPUS_EXPECT_OK;
        strcpy(expect->token, "OK");
        return 1;
    }

    size_t line_end = 0;
    while (line_end < len && buf[line_end] != '\n' && buf[line_end] != '\r')
        line_end++;

    const char needle[] = "EXPECT:";
    size_t needle_len = sizeof(needle) - 1u;
    size_t pos = line_end;
    for (size_t i = 0; i + needle_len <= line_end; i++) {
        if (memcmp(buf + i, needle, needle_len) == 0) {
            pos = i + needle_len;
            break;
        }
    }
    if (pos == line_end)
        return 0;

    while (pos < line_end && (buf[pos] == ' ' || buf[pos] == '\t'))
        pos++;

    size_t out = 0;
    while (pos < line_end && out + 1u < sizeof(expect->token)) {
        uint8_t c = buf[pos];
        if (c == ' ' || c == '\t' || c == '*' || c == '/')
            break;
        expect->token[out++] = (char)c;
        pos++;
    }
    expect->token[out] = '\0';
    if (strcmp(expect->token, "OK") == 0) {
        expect->kind = CORPUS_EXPECT_OK;
        return 1;
    }
    if (strcmp(expect->token, "FAIL") == 0) {
        expect->kind = CORPUS_EXPECT_FAIL;
        return 1;
    }

    expect->code = diag_from_token(expect->token);
    if (expect->code == MAELYS_DATALOG_DIAG_NONE)
        return 0;
    expect->kind = CORPUS_EXPECT_DIAG;
    return 1;
}

static int run_corpus_file(const char *path)
{
    uint8_t *buf = NULL;
    size_t len = 0;
    int api_rc = read_file_binary(path, &buf, &len);
    if (api_rc != MAELYS_OK) {
        TEST_NOTE_ERR("failed to read %s", path);
        return 1;
    }

    corpus_expect_t expect;
    if (!parse_expect(buf, len, &expect)) {
        TEST_NOTE_ERR("missing or invalid EXPECT header: %s", path);
        free(buf);
        return 1;
    }

    maelys_datalog_ruleset_t ruleset;
    api_rc = init_corpus_ruleset(&ruleset);
    if (api_rc != MAELYS_OK) {
        TEST_NOTE_ERR("failed to init corpus ruleset for %s", path);
        free(buf);
        return 1;
    }

    maelys_datalog_diagnostic_t diag = {0};
    api_rc = maelys_datalog_parse_ruleset_ex(
        &ruleset, (const char *)buf, len, path, &diag);

    if (expect.kind == CORPUS_EXPECT_OK) {
        if (api_rc != MAELYS_OK || diag.code != MAELYS_DATALOG_DIAG_NONE) {
            TEST_NOTE_ERR("%s expected OK, got rc=%d diag=%s",
                          path,
                          api_rc,
                          maelys_datalog_diag_code_name(diag.code));
            maelys_datalog_ruleset_clear(&ruleset);
            free(buf);
            return 1;
        }
    } else if (expect.kind == CORPUS_EXPECT_FAIL) {
        if (api_rc == MAELYS_OK) {
            TEST_NOTE_ERR("%s expected FAIL, got OK", path);
            maelys_datalog_ruleset_clear(&ruleset);
            free(buf);
            return 1;
        }
    } else {
        if (api_rc == MAELYS_OK || diag.code != expect.code) {
            TEST_NOTE_ERR("%s expected %s, got rc=%d diag=%s",
                          path,
                          expect.token,
                          api_rc,
                          maelys_datalog_diag_code_name(diag.code));
            maelys_datalog_ruleset_clear(&ruleset);
            free(buf);
            return 1;
        }
    }

    maelys_datalog_ruleset_clear(&ruleset);
    free(buf);
    return 0;
}

static int collect_corpus_files(const char *dir, corpus_file_list_t *list)
{
    DIR *dp = opendir(dir);
    if (!dp)
        return MAELYS_ERR_INVALID_FIELD;

    struct dirent *ent;
    while ((ent = readdir(dp)) != NULL) {
        if (strcmp(ent->d_name, ".") == 0 || strcmp(ent->d_name, "..") == 0)
            continue;

        char path[MAX_CORPUS_PATH];
        int n = snprintf(path, sizeof(path), "%s/%s", dir, ent->d_name);
        if (n < 0 || (size_t)n >= sizeof(path)) {
            closedir(dp);
            return MAELYS_ERR_PAYLOAD_TOO_LARGE;
        }

        struct stat st;
        if (stat(path, &st) != 0) {
            closedir(dp);
            return MAELYS_ERR_INVALID_FIELD;
        }
        if (S_ISDIR(st.st_mode)) {
            int rc = collect_corpus_files(path, list);
            if (rc != MAELYS_OK) {
                closedir(dp);
                return rc;
            }
        } else if (S_ISREG(st.st_mode) && has_suffix(path, ".dl")) {
            if (list->count >= MAX_CORPUS_FILES) {
                closedir(dp);
                return MAELYS_ERR_PAYLOAD_TOO_LARGE;
            }
            snprintf(list->paths[list->count],
                     sizeof(list->paths[list->count]),
                     "%s",
                     path);
            list->count++;
        }
    }
    closedir(dp);
    return MAELYS_OK;
}

static int cmp_paths(const void *a, const void *b)
{
    const char *pa = (const char *)a;
    const char *pb = (const char *)b;
    return strcmp(pa, pb);
}

static int test_corpus_files(void)
{
    TEST_BEGIN();
    corpus_file_list_t list = {0};
    int result = collect_corpus_files(CORPUS_ROOT, &list);
    TEST_ASSERT_EQUAL(MAELYS_OK, result, "%d");
    TEST_ASSERT_TRUE(list.count > 0u);

    qsort(list.paths, list.count, sizeof(list.paths[0]), cmp_paths);

    size_t failed = 0;
    for (size_t i = 0; i < list.count; i++) {
        if (run_corpus_file(list.paths[i]) != 0) {
            failed++;
            TEST_NOTE_ERR("corpus mismatch: %s", list.paths[i]);
        }
    }
    TEST_NOTE_INFO("corpus files exercised: %zu", list.count);
    TEST_ASSERT_EQUAL((size_t)0u, failed, "%zu");
    TEST_END();
}

static int test_registry_conflict_api_fixture(void)
{
    TEST_BEGIN();
    maelys_datalog_domain_def_t domain = {
        .domain_name = k_conflict_domain,
        .predicates = k_conflict_domain_table,
        .predicate_count =
            sizeof(k_conflict_domain_table) / sizeof(k_conflict_domain_table[0]),
        .description = "Corpus registry conflict fixture",
        .install_predicates = NULL,
    };
    int result = maelys_datalog_domain_registry_register(&domain);
    TEST_ASSERT_EQUAL(MAELYS_OK, result, "%d");

    const char src[] = "allow(X) :- safe(X).\n";
    char sha[65];
    (void)maelys_sha256_hex((const unsigned char *)src, strlen(src), sha);

    char manifest[2048];
    snprintf(manifest,
             sizeof(manifest),
             "{\"policy_set_id\":\"corpus.registry\",\"policy_set_version\":\"1\","
             "\"manifest_version\":\"1\","
             "\"policies\":[{\"policy_id\":\"%s\",\"domain\":\"%s\","
             "\"file\":\"ignored.dl\",\"sha256\":\"%s\",\"mode\":\"shadow\","
             "\"enabled\":true,\"description\":\"registry\","
             "\"queries\":[{\"name\":\"unknown\",\"arity\":1}]}],"
             "\"capabilities\":[],\"default_profile\":\"MAELYS-DATALOG-TEXT-v1\","
             "\"strict_loading\":true,\"fail_closed\":true,\"created_for\":\"test\"}",
             k_conflict_policy_id,
             k_conflict_domain,
             sha);

    maelys_datalog_policy_bundle_entry_t bundle = {
        .policy_id = k_conflict_policy_id,
        .src = src,
        .src_len = strlen(src),
    };
    maelys_datalog_policy_set_t set;
    maelys_datalog_diagnostic_t diag = {0};
    result = maelys_datalog_manifest_load_from_text(manifest,
                                                    strlen(manifest),
                                                    &bundle,
                                                    1u,
                                                    0,
                                                    &set,
                                                    &diag);
    TEST_ASSERT_EQUAL(MAELYS_ERR_INVALID_FIELD, result, "%d");
    TEST_ASSERT_EQUAL((int)MAELYS_DATALOG_DIAG_REGISTRY_CONFLICT,
                      (int)diag.code,
                      "%d");
    TEST_ASSERT_EQUAL_STRING("unknown", diag.predicate);
    TEST_ASSERT_EQUAL((size_t)0u, set.policy_count, "%zu");
    TEST_END();
}

int main(int argc, char **argv)
{
    test_case_t cases[] = {
        {"maelys_datalog_corpus/files",
         TEST_MODE_NON_BLOCKING,
         test_corpus_files},
        {"maelys_datalog_corpus/registry_conflict_api",
         TEST_MODE_NON_BLOCKING,
         test_registry_conflict_api_fixture},
    };
    return test_main("maelys_datalog_corpus",
                     cases,
                     (int)(sizeof(cases) / sizeof(cases[0])),
                     argc,
                     argv);
}
