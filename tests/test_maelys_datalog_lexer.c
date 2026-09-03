#include "src/core/maelys_datalog_lexer.h"
#include "tests/helpers/test_framework.h"

#include <string.h>

static int test_lexer_valid_tokens_comments_crlf(void) {
    TEST_BEGIN();
    const char *src = "% comment\r\nblocked(\"proj-1\").\n/* block */\nallow(P) :- blocked(P), P = \"proj-1\".";
    TEST_ASSERT_EQUAL(MAELYS_OK, maelys_datalog_lexer_validate(src, strlen(src)), "%d");
    maelys_datalog_lexer_t l;
    maelys_datalog_token_t t;
    TEST_ASSERT_EQUAL(MAELYS_OK, maelys_datalog_lexer_init(&l, src, strlen(src)), "%d");
    TEST_ASSERT_EQUAL(MAELYS_OK, maelys_datalog_lexer_next(&l, &t), "%d");
    TEST_ASSERT_EQUAL(MAELYS_DATALOG_TOKEN_PREDICATE, t.kind, "%d");
    TEST_ASSERT_EQUAL_STRING("blocked", t.text);
    TEST_END();
}

static int test_lexer_rejects_unsupported_syntax(void) {
    TEST_BEGIN();
    const char *query = "?- blocked(\"proj-1\").";
    const char *pragma = ".pragma foo.";
    const char *tilde = "~blocked(\"proj-1\").";
    const char *upper_not = "allow(P) :- NOT(blocked(P)).";
    const char *amp = "a(X) :- b(X) & c(X).";
    const char *matches = "a(X) :- X MATCHES \"x\".";
    TEST_ASSERT_EQUAL(MAELYS_ERR_UNSUPPORTED, maelys_datalog_lexer_validate(query, strlen(query)), "%d");
    TEST_ASSERT_EQUAL(MAELYS_ERR_UNSUPPORTED, maelys_datalog_lexer_validate(pragma, strlen(pragma)), "%d");
    TEST_ASSERT_EQUAL(MAELYS_ERR_UNSUPPORTED, maelys_datalog_lexer_validate(tilde, strlen(tilde)), "%d");
    TEST_ASSERT_EQUAL(MAELYS_ERR_UNSUPPORTED, maelys_datalog_lexer_validate(upper_not, strlen(upper_not)), "%d");
    TEST_ASSERT_EQUAL(MAELYS_ERR_UNSUPPORTED, maelys_datalog_lexer_validate(amp, strlen(amp)), "%d");
    TEST_ASSERT_EQUAL(MAELYS_ERR_UNSUPPORTED, maelys_datalog_lexer_validate(matches, strlen(matches)), "%d");
    TEST_END();
}

static int test_lexer_bounds_and_utf8(void) {
    TEST_BEGIN();
    char ok[1100];
    memset(ok, 'a', sizeof(ok));
    ok[0] = '"';
    ok[1025] = '"';
    ok[1026] = '\0';
    TEST_ASSERT_EQUAL(MAELYS_OK, maelys_datalog_lexer_validate(ok, 1026), "%d");
    ok[1025] = 'a';
    ok[1026] = '"';
    ok[1027] = '\0';
    TEST_ASSERT_EQUAL(MAELYS_ERR_PAYLOAD_TOO_LARGE, maelys_datalog_lexer_validate(ok, 1027), "%d");
    const char non_ascii_pred[] = "\xc3\xa9(\"proj-1\").";
    TEST_ASSERT_EQUAL(MAELYS_ERR_INVALID_FIELD, maelys_datalog_lexer_validate(non_ascii_pred, sizeof(non_ascii_pred) - 1), "%d");
    const char invalid_utf8[] = { 'p', '(', '"', (char)0xff, '"', ')', '.', '\0' };
    TEST_ASSERT_EQUAL(MAELYS_ERR_INVALID_FIELD, maelys_datalog_lexer_validate(invalid_utf8, 7), "%d");
    const char *nested_comment = "/* a /* b */ */";
    TEST_ASSERT_EQUAL(MAELYS_ERR_UNSUPPORTED, maelys_datalog_lexer_validate(nested_comment, strlen(nested_comment)), "%d");
    TEST_END();
}

static int test_lexer_token_not_exact_keyword(void) {
    TEST_BEGIN();
    maelys_datalog_lexer_t l;
    maelys_datalog_token_t t;
    const char *src = "not not_foo";
    TEST_ASSERT_EQUAL(MAELYS_OK, maelys_datalog_lexer_init(&l, src, strlen(src)), "%d");
    TEST_ASSERT_EQUAL(MAELYS_OK, maelys_datalog_lexer_next(&l, &t), "%d");
    TEST_ASSERT_EQUAL(MAELYS_DATALOG_TOKEN_NOT, t.kind, "%d");
    TEST_ASSERT_EQUAL_STRING("not", t.text);
    TEST_ASSERT_EQUAL(MAELYS_OK, maelys_datalog_lexer_next(&l, &t), "%d");
    TEST_ASSERT_EQUAL(MAELYS_DATALOG_TOKEN_PREDICATE, t.kind, "%d");
    TEST_ASSERT_EQUAL_STRING("not_foo", t.text);
    TEST_END();
}

static int test_lexer_diag_unsupported_matches(void) {
    TEST_BEGIN();
    maelys_datalog_diagnostic_t diag;
    const char *src = "a(X) :- X MATCHES \"x\".";
    TEST_ASSERT_EQUAL(MAELYS_ERR_UNSUPPORTED,
                      maelys_datalog_lexer_validate_ex(src, strlen(src), "test.dl", &diag),
                      "%d");
    TEST_ASSERT_EQUAL(MAELYS_DATALOG_DIAG_LEXER_UNSUPPORTED_CONSTRUCT, diag.code, "%d");
    TEST_ASSERT_EQUAL_STRING("MATCHES", diag.token);
    TEST_END();
}

static int test_lexer_diag_invalid_utf8(void) {
    TEST_BEGIN();
    const char invalid_utf8[] = { 'p', '(', '"', (char)0xff, '"', ')', '.', '\0' };
    maelys_datalog_diagnostic_t diag;
    TEST_ASSERT_EQUAL(MAELYS_ERR_INVALID_FIELD,
                      maelys_datalog_lexer_validate_ex(invalid_utf8, 7, "test.dl", &diag),
                      "%d");
    TEST_ASSERT_EQUAL(MAELYS_DATALOG_DIAG_LEXER_INVALID_UTF8, diag.code, "%d");
    TEST_ASSERT_EQUAL_STRING("lexer", diag.phase);
    TEST_END();
}

static int test_lexer_diag_string_too_long(void) {
    TEST_BEGIN();
    char text[1100];
    memset(text, 'a', sizeof(text));
    text[0] = '"';
    text[1026] = '"';
    text[1027] = '\0';
    maelys_datalog_diagnostic_t diag;
    TEST_ASSERT_EQUAL(MAELYS_ERR_PAYLOAD_TOO_LARGE,
                      maelys_datalog_lexer_validate_ex(text, 1027, "test.dl", &diag),
                      "%d");
    TEST_ASSERT_EQUAL(MAELYS_DATALOG_DIAG_LEXER_STRING_TOO_LONG, diag.code, "%d");
    TEST_ASSERT_EQUAL((size_t)MAELYS_DATALOG_MAX_STRING_BYTES + 1u, diag.count, "%zu");
    TEST_ASSERT_EQUAL((size_t)MAELYS_DATALOG_MAX_STRING_BYTES, diag.limit, "%zu");
    TEST_END();
}

static int test_datalog_lexer_wildcard_token(void) {
    TEST_BEGIN();
    maelys_datalog_lexer_t l;
    maelys_datalog_token_t t;
    const char *src = "p(_).";
    maelys_result_t er = maelys_datalog_lexer_init(&l, src, strlen(src));
    TEST_ASSERT_EQUAL(MAELYS_OK, er, "%d");
    er = maelys_datalog_lexer_next(&l, &t);
    TEST_ASSERT_EQUAL(MAELYS_OK, er, "%d");
    TEST_ASSERT_EQUAL(MAELYS_DATALOG_TOKEN_PREDICATE, t.kind, "%d");
    TEST_ASSERT_EQUAL_STRING("p", t.text);
    er = maelys_datalog_lexer_next(&l, &t);
    TEST_ASSERT_EQUAL(MAELYS_OK, er, "%d");
    TEST_ASSERT_EQUAL(MAELYS_DATALOG_TOKEN_LPAREN, t.kind, "%d");
    er = maelys_datalog_lexer_next(&l, &t);
    TEST_ASSERT_EQUAL(MAELYS_OK, er, "%d");
    TEST_ASSERT_EQUAL(MAELYS_DATALOG_TOKEN_UNDERSCORE, t.kind, "%d");
    TEST_ASSERT_EQUAL_STRING("_", t.text);
    er = maelys_datalog_lexer_next(&l, &t);
    TEST_ASSERT_EQUAL(MAELYS_OK, er, "%d");
    TEST_ASSERT_EQUAL(MAELYS_DATALOG_TOKEN_RPAREN, t.kind, "%d");
    TEST_END();
}

static int test_datalog_lexer_string_underscore_not_wildcard(void) {
    TEST_BEGIN();
    maelys_datalog_lexer_t l;
    maelys_datalog_token_t t;
    TEST_ASSERT_EQUAL(MAELYS_OK, maelys_datalog_lexer_init(&l, "\"_\"", 3), "%d");
    TEST_ASSERT_EQUAL(MAELYS_OK, maelys_datalog_lexer_next(&l, &t), "%d");
    TEST_ASSERT_EQUAL(MAELYS_DATALOG_TOKEN_STRING, t.kind, "%d");
    TEST_ASSERT_EQUAL_STRING("_", t.text);
    TEST_END();
}

static int test_datalog_lexer_identifier_with_underscore_not_split(void) {
    TEST_BEGIN();
    maelys_datalog_lexer_t l;
    maelys_datalog_token_t t;
    const char *src = "blocked_backend(P).";
    TEST_ASSERT_EQUAL(MAELYS_OK, maelys_datalog_lexer_init(&l, src, strlen(src)), "%d");
    TEST_ASSERT_EQUAL(MAELYS_OK, maelys_datalog_lexer_next(&l, &t), "%d");
    TEST_ASSERT_EQUAL(MAELYS_DATALOG_TOKEN_PREDICATE, t.kind, "%d");
    TEST_ASSERT_EQUAL_STRING("blocked_backend", t.text);
    TEST_END();
}

static int test_datalog_lexer_comment_underscore_ignored(void) {
    TEST_BEGIN();
    maelys_datalog_lexer_t l;
    maelys_datalog_token_t t;
    const char *src = "% _ ignored\nblocked(P).";
    TEST_ASSERT_EQUAL(MAELYS_OK, maelys_datalog_lexer_init(&l, src, strlen(src)), "%d");
    TEST_ASSERT_EQUAL(MAELYS_OK, maelys_datalog_lexer_next(&l, &t), "%d");
    TEST_ASSERT_EQUAL(MAELYS_DATALOG_TOKEN_PREDICATE, t.kind, "%d");
    TEST_ASSERT_EQUAL_STRING("blocked", t.text);
    TEST_END();
}

static int test_datalog_lexer_leading_underscore_identifier_not_split(void) {
    TEST_BEGIN();
    maelys_datalog_diagnostic_t diag;
    TEST_ASSERT_EQUAL(MAELYS_ERR_UNSUPPORTED,
                      maelys_datalog_lexer_validate_ex("_foo", 4, "test.dl", &diag),
                      "%d");
    TEST_ASSERT_EQUAL(MAELYS_DATALOG_DIAG_LEXER_UNSUPPORTED_CONSTRUCT, diag.code, "%d");
    TEST_ASSERT_EQUAL_STRING("_", diag.token);
    TEST_END();
}

int main(int argc, char **argv) {
    test_case_t cases[] = {
        {"maelys_datalog_lexer/valid_tokens_comments_crlf", TEST_MODE_NON_BLOCKING, test_lexer_valid_tokens_comments_crlf},
        {"maelys_datalog_lexer/rejects_unsupported_syntax", TEST_MODE_NON_BLOCKING, test_lexer_rejects_unsupported_syntax},
        {"maelys_datalog_lexer/bounds_and_utf8", TEST_MODE_NON_BLOCKING, test_lexer_bounds_and_utf8},
        {"maelys_datalog_lexer/token_not_exact_keyword", TEST_MODE_NON_BLOCKING, test_lexer_token_not_exact_keyword},
        {"maelys_datalog_lexer/diag_unsupported_matches", TEST_MODE_NON_BLOCKING, test_lexer_diag_unsupported_matches},
        {"maelys_datalog_lexer/diag_invalid_utf8", TEST_MODE_NON_BLOCKING, test_lexer_diag_invalid_utf8},
        {"maelys_datalog_lexer/diag_string_too_long", TEST_MODE_NON_BLOCKING, test_lexer_diag_string_too_long},
        {"maelys_datalog_lexer/wildcard_token", TEST_MODE_NON_BLOCKING, test_datalog_lexer_wildcard_token},
        {"maelys_datalog_lexer/string_underscore_not_wildcard", TEST_MODE_NON_BLOCKING, test_datalog_lexer_string_underscore_not_wildcard},
        {"maelys_datalog_lexer/identifier_with_underscore_not_split", TEST_MODE_NON_BLOCKING, test_datalog_lexer_identifier_with_underscore_not_split},
        {"maelys_datalog_lexer/comment_underscore_ignored", TEST_MODE_NON_BLOCKING, test_datalog_lexer_comment_underscore_ignored},
        {"maelys_datalog_lexer/leading_underscore_identifier_not_split", TEST_MODE_NON_BLOCKING, test_datalog_lexer_leading_underscore_identifier_not_split},
    };
    return test_main("maelys_datalog_lexer", cases, (int)(sizeof(cases) / sizeof(cases[0])), argc, argv);
}
