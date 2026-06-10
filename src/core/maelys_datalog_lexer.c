#include "src/core/maelys_datalog_lexer.h"

#include "common/maelys_utf8.h"

#include <ctype.h>
#include <stdio.h>
#include <string.h>

static int is_pred_start(unsigned char c) { return c >= 'a' && c <= 'z'; }
static int is_pred_char(unsigned char c) { return (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_'; }
static int is_var_start(unsigned char c) { return c >= 'A' && c <= 'Z'; }
static int is_var_char(unsigned char c) { return (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_'; }

maelys_result_t maelys_datalog_lexer_init(maelys_datalog_lexer_t *lexer,
                                          const char *src,
                                          size_t len) {
    return maelys_datalog_lexer_init_ex(lexer, src, len, NULL, NULL);
}

maelys_result_t maelys_datalog_lexer_init_ex(maelys_datalog_lexer_t *lexer,
                                             const char *src,
                                             size_t len,
                                             const char *file_path,
                                             maelys_datalog_diagnostic_t *out_diag) {
    if (!lexer || (!src && len > 0)) return MAELYS_ERR_INVALID_ARGUMENT;
    if (out_diag) maelys_datalog_diagnostic_clear(out_diag);
    if (!maelys_utf8_validate((const unsigned char *)src, len)) {
        maelys_datalog_diagnostic_set(out_diag,
                                      MAELYS_DATALOG_DIAG_LEXER_INVALID_UTF8,
                                      "lexer",
                                      file_path,
                                      0,
                                      0,
                                      "invalid UTF-8 in policy text",
                                      "ensure the policy text is valid UTF-8");
        return MAELYS_ERR_INVALID_FIELD;
    }
    lexer->src = src ? src : "";
    lexer->len = len;
    lexer->pos = 0;
    lexer->line = 1;
    lexer->column = 1;
    lexer->error = MAELYS_OK;
    lexer->file_path = file_path;
    lexer->diag = out_diag;
    return MAELYS_OK;
}

static void diag_token(maelys_datalog_lexer_t *l, const char *token, size_t len) {
    if (!l || !l->diag) return;
    if (!token) token = "";
    size_t n = len;
    if (n >= sizeof(l->diag->token)) n = sizeof(l->diag->token) - 1u;
    memcpy(l->diag->token, token, n);
    l->diag->token[n] = '\0';
}

static maelys_result_t lexer_invalid(maelys_datalog_lexer_t *l,
                                     maelys_datalog_diag_code_t code,
                                     maelys_result_t rc,
                                     const char *token,
                                     size_t len,
                                     const char *message,
                                     const char *hint) {
    maelys_datalog_diagnostic_set(l ? l->diag : NULL,
                                  code,
                                  "lexer",
                                  l ? l->file_path : NULL,
                                  l ? l->line : 0,
                                  l ? l->column : 0,
                                  message,
                                  hint);
    diag_token(l, token, len);
    return rc;
}

static int peek(maelys_datalog_lexer_t *l, size_t off) {
    return l->pos + off < l->len ? (unsigned char)l->src[l->pos + off] : -1;
}

static int take(maelys_datalog_lexer_t *l) {
    if (l->pos >= l->len) return -1;
    unsigned char c = (unsigned char)l->src[l->pos++];
    if (c == '\n') {
        l->line++;
        l->column = 1;
    } else {
        l->column++;
    }
    return c;
}

static maelys_result_t skip_ws_comments(maelys_datalog_lexer_t *l) {
    for (;;) {
        int c = peek(l, 0);
        if (c < 0) return MAELYS_OK;
        if (c == ' ' || c == '\t' || c == '\r' || c == '\n') {
            take(l);
            continue;
        }
        if (c == '%') {
            while (peek(l, 0) >= 0 && peek(l, 0) != '\n') take(l);
            continue;
        }
        if (c == '/' && peek(l, 1) == '*') {
            take(l); take(l);
            int closed = 0;
            while (peek(l, 0) >= 0) {
            if (peek(l, 0) == '/' && peek(l, 1) == '*') return MAELYS_ERR_UNSUPPORTED;
                if (peek(l, 0) == '*' && peek(l, 1) == '/') {
                    take(l); take(l);
                    closed = 1;
                    break;
                }
                take(l);
            }
            if (!closed) {
                return lexer_invalid(l,
                                     MAELYS_DATALOG_DIAG_LEXER_INVALID_TOKEN,
                                     MAELYS_ERR_INVALID_FIELD,
                                     "/*",
                                     2,
                                     "unterminated block comment",
                                     "close the block comment before end of file");
            }
            continue;
        }
        return MAELYS_OK;
    }
}

static maelys_result_t emit_text(maelys_datalog_token_t *out,
                                 maelys_datalog_token_kind_t kind,
                                 const char *src,
                                 size_t len) {
    if (len > MAELYS_DATALOG_MAX_TOKEN_BYTES) return MAELYS_ERR_PAYLOAD_TOO_LARGE;
    out->kind = kind;
    out->len = len;
    memcpy(out->text, src, len);
    out->text[len] = '\0';
    return MAELYS_OK;
}

maelys_result_t maelys_datalog_lexer_next(maelys_datalog_lexer_t *l,
                                          maelys_datalog_token_t *out) {
    if (!l || !out) return MAELYS_ERR_INVALID_ARGUMENT;
    memset(out, 0, sizeof(*out));
    maelys_result_t rc = skip_ws_comments(l);
    if (rc != MAELYS_OK) return rc;
    out->line = l->line;
    out->column = l->column;
    int c = peek(l, 0);
    if (c < 0) {
        out->kind = MAELYS_DATALOG_TOKEN_EOF;
        return MAELYS_OK;
    }

    if (is_pred_start((unsigned char)c)) {
        size_t start = l->pos;
        while (is_pred_char((unsigned char)peek(l, 0))) take(l);
        size_t len = l->pos - start;
        if (len == 3 && memcmp(l->src + start, "not", 3) == 0) {
            return emit_text(out, MAELYS_DATALOG_TOKEN_NOT, l->src + start, len);
        }
        if ((len == 3 && memcmp(l->src + start, "AND", 3) == 0) ||
            (len == 7 && memcmp(l->src + start, "MATCHES", 7) == 0)) {
            return lexer_invalid(l,
                                 MAELYS_DATALOG_DIAG_LEXER_UNSUPPORTED_CONSTRUCT,
                                 MAELYS_ERR_UNSUPPORTED,
                                 l->src + start,
                                 len,
                                 "unsupported Datalog construct",
                                 "construct is not supported by MAELYS-DATALOG-TEXT-v1");
        }
        rc = emit_text(out, MAELYS_DATALOG_TOKEN_PREDICATE, l->src + start, len);
        if (rc == MAELYS_OK && strcmp(out->text, "true") == 0) { out->kind = MAELYS_DATALOG_TOKEN_BOOLEAN; out->boolean = 1; }
        if (rc == MAELYS_OK && strcmp(out->text, "false") == 0) { out->kind = MAELYS_DATALOG_TOKEN_BOOLEAN; out->boolean = 0; }
        return rc;
    }
    if (is_var_start((unsigned char)c)) {
        size_t start = l->pos;
        while (is_var_char((unsigned char)peek(l, 0))) take(l);
        size_t len = l->pos - start;
        if ((len == 3 && memcmp(l->src + start, "NOT", 3) == 0) ||
            (len == 3 && memcmp(l->src + start, "AND", 3) == 0) ||
            (len == 7 && memcmp(l->src + start, "MATCHES", 7) == 0)) {
            return lexer_invalid(l,
                                 MAELYS_DATALOG_DIAG_LEXER_UNSUPPORTED_CONSTRUCT,
                                 MAELYS_ERR_UNSUPPORTED,
                                 l->src + start,
                                 len,
                                 "unsupported Datalog construct",
                                 "construct is not supported by MAELYS-DATALOG-TEXT-v1");
        }
        return emit_text(out, MAELYS_DATALOG_TOKEN_VARIABLE, l->src + start, len);
    }
    if (c >= '0' && c <= '9') {
        size_t start = l->pos;
        long long val = 0;
        while (peek(l, 0) >= '0' && peek(l, 0) <= '9') {
            val = (val * 10) + (take(l) - '0');
            if (val > MAELYS_DATALOG_MAX_INT) return MAELYS_ERR_PAYLOAD_TOO_LARGE;
        }
        if (peek(l, 0) == '.' && peek(l, 1) >= '0' && peek(l, 1) <= '9') {
            return lexer_invalid(l,
                                 MAELYS_DATALOG_DIAG_LEXER_UNSUPPORTED_CONSTRUCT,
                                 MAELYS_ERR_UNSUPPORTED,
                                 l->src + start,
                                 l->pos - start + 2u,
                                 "unsupported numeric literal",
                                 "floats are not supported by MAELYS-DATALOG-TEXT-v1");
        }
        maelys_result_t er = emit_text(out, MAELYS_DATALOG_TOKEN_INTEGER, l->src + start, l->pos - start);
        out->integer = val;
        return er;
    }
    if (c == '"') {
        take(l);
        size_t start = l->pos;
        size_t bytes = 0;
        while (peek(l, 0) >= 0 && peek(l, 0) != '"') {
            if (peek(l, 0) == '\\') {
                return lexer_invalid(l,
                                     MAELYS_DATALOG_DIAG_LEXER_UNSUPPORTED_CONSTRUCT,
                                     MAELYS_ERR_UNSUPPORTED,
                                     "\\",
                                     1,
                                     "unsupported string escape",
                                     "string escapes are not supported by MAELYS-DATALOG-TEXT-v1");
            }
            take(l);
            bytes++;
            if (bytes > MAELYS_DATALOG_MAX_STRING_BYTES) {
                maelys_result_t er = lexer_invalid(l,
                                                   MAELYS_DATALOG_DIAG_LEXER_STRING_TOO_LONG,
                                                   MAELYS_ERR_PAYLOAD_TOO_LARGE,
                                                   "",
                                                   0,
                                                   "string literal too long",
                                                   "shorten the string atom or register a bounded atom");
                maelys_datalog_diagnostic_set_limit(l->diag, bytes, MAELYS_DATALOG_MAX_STRING_BYTES);
                return er;
            }
        }
        if (peek(l, 0) != '"') {
            return lexer_invalid(l,
                                 MAELYS_DATALOG_DIAG_LEXER_INVALID_TOKEN,
                                 MAELYS_ERR_INVALID_FIELD,
                                 "\"",
                                 1,
                                 "unterminated string literal",
                                 "close the string literal before end of file");
        }
        size_t len = l->pos - start;
        take(l);
        return emit_text(out, MAELYS_DATALOG_TOKEN_STRING, l->src + start, len);
    }

    c = take(l);
    switch (c) {
        case '_' :
            if (isalnum((unsigned char)peek(l, 0)) || peek(l, 0) == '_') {
                return lexer_invalid(l,
                                     MAELYS_DATALOG_DIAG_LEXER_UNSUPPORTED_CONSTRUCT,
                                     MAELYS_ERR_UNSUPPORTED,
                                     l->src + l->pos - 1u,
                                     1,
                                     "unsupported Datalog construct",
                                     "underscore is only supported as an isolated anonymous variable");
            }
            return emit_text(out, MAELYS_DATALOG_TOKEN_UNDERSCORE, l->src + l->pos - 1u, 1);
        case '(' : out->kind = MAELYS_DATALOG_TOKEN_LPAREN; return MAELYS_OK;
        case ')' : out->kind = MAELYS_DATALOG_TOKEN_RPAREN; return MAELYS_OK;
        case ',' : out->kind = MAELYS_DATALOG_TOKEN_COMMA; return MAELYS_OK;
        case '.' :
            if (is_pred_start((unsigned char)peek(l, 0))) {
                return lexer_invalid(l,
                                     MAELYS_DATALOG_DIAG_LEXER_UNSUPPORTED_CONSTRUCT,
                                     MAELYS_ERR_UNSUPPORTED,
                                     ".",
                                     1,
                                     "unsupported directive",
                                     "construct is not supported by MAELYS-DATALOG-TEXT-v1");
            }
            out->kind = MAELYS_DATALOG_TOKEN_DOT; return MAELYS_OK;
        case ':' :
            if (peek(l, 0) == '-') { take(l); out->kind = MAELYS_DATALOG_TOKEN_NECK; return MAELYS_OK; }
            return lexer_invalid(l,
                                 MAELYS_DATALOG_DIAG_LEXER_INVALID_TOKEN,
                                 MAELYS_ERR_INVALID_FIELD,
                                 ":",
                                 1,
                                 "invalid token",
                                 "use :- to introduce a rule body");
        case '=' : out->kind = MAELYS_DATALOG_TOKEN_EQ; return MAELYS_OK;
        case '!' :
            if (peek(l, 0) == '=') { take(l); out->kind = MAELYS_DATALOG_TOKEN_NEQ; return MAELYS_OK; }
            return lexer_invalid(l,
                                 MAELYS_DATALOG_DIAG_LEXER_UNSUPPORTED_CONSTRUCT,
                                 MAELYS_ERR_UNSUPPORTED,
                                 "!",
                                 1,
                                 "unsupported Datalog construct",
                                 "construct is not supported by MAELYS-DATALOG-TEXT-v1");
        case '<' :
            if (peek(l, 0) == '=') { take(l); out->kind = MAELYS_DATALOG_TOKEN_LTE; return MAELYS_OK; }
            if (peek(l, 0) == '-') {
                return lexer_invalid(l,
                                     MAELYS_DATALOG_DIAG_LEXER_UNSUPPORTED_CONSTRUCT,
                                     MAELYS_ERR_UNSUPPORTED,
                                     "<-",
                                     2,
                                     "unsupported rule neck",
                                     "use :- to introduce a rule body");
            }
            out->kind = MAELYS_DATALOG_TOKEN_LT; return MAELYS_OK;
        case '>' :
            if (peek(l, 0) == '=') { take(l); out->kind = MAELYS_DATALOG_TOKEN_GTE; return MAELYS_OK; }
            out->kind = MAELYS_DATALOG_TOKEN_GT; return MAELYS_OK;
        case '?' :
        case '~' :
        case ';' :
        case '|' :
        case '&' :
            return lexer_invalid(l,
                                 MAELYS_DATALOG_DIAG_LEXER_UNSUPPORTED_CONSTRUCT,
                                 MAELYS_ERR_UNSUPPORTED,
                                 l->src + l->pos - 1u,
                                 1,
                                 "unsupported Datalog construct",
                                 "construct is not supported by MAELYS-DATALOG-TEXT-v1");
        default:
            return lexer_invalid(l,
                                 MAELYS_DATALOG_DIAG_LEXER_INVALID_TOKEN,
                                 MAELYS_ERR_INVALID_FIELD,
                                 l->src + l->pos - 1u,
                                 1,
                                 "invalid token",
                                 "use MAELYS-DATALOG-TEXT-v1 syntax");
    }
}

maelys_result_t maelys_datalog_lexer_validate(const char *src, size_t len) {
    return maelys_datalog_lexer_validate_ex(src, len, NULL, NULL);
}

maelys_result_t maelys_datalog_lexer_validate_ex(const char *src,
                                                 size_t len,
                                                 const char *file_path,
                                                 maelys_datalog_diagnostic_t *out_diag) {
    maelys_datalog_lexer_t l;
    maelys_result_t rc = maelys_datalog_lexer_init_ex(&l, src, len, file_path, out_diag);
    if (rc != MAELYS_OK) return rc;
    maelys_datalog_token_t tok;
    do {
        rc = maelys_datalog_lexer_next(&l, &tok);
        if (rc != MAELYS_OK) return rc;
    } while (tok.kind != MAELYS_DATALOG_TOKEN_EOF);
    return MAELYS_OK;
}
