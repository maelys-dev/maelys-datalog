#pragma once
#ifndef MAELYS_DATALOG_LEXER_H
#define MAELYS_DATALOG_LEXER_H

#include "src/core/maelys_datalog_diagnostic.h"
#include "src/core/maelys_datalog_types.h"

#include "common/maelys_errors.h"

typedef enum {
    MAELYS_DATALOG_TOKEN_EOF = 0,
    MAELYS_DATALOG_TOKEN_PREDICATE,
    MAELYS_DATALOG_TOKEN_VARIABLE,
    MAELYS_DATALOG_TOKEN_STRING,
    MAELYS_DATALOG_TOKEN_INTEGER,
    MAELYS_DATALOG_TOKEN_BOOLEAN,
    MAELYS_DATALOG_TOKEN_UNDERSCORE,
    MAELYS_DATALOG_TOKEN_LPAREN,
    MAELYS_DATALOG_TOKEN_RPAREN,
    MAELYS_DATALOG_TOKEN_COMMA,
    MAELYS_DATALOG_TOKEN_DOT,
    MAELYS_DATALOG_TOKEN_NECK,
    MAELYS_DATALOG_TOKEN_EQ,
    MAELYS_DATALOG_TOKEN_NEQ,
    MAELYS_DATALOG_TOKEN_LT,
    MAELYS_DATALOG_TOKEN_LTE,
    MAELYS_DATALOG_TOKEN_GT,
    MAELYS_DATALOG_TOKEN_GTE,
    MAELYS_DATALOG_TOKEN_NOT,
    MAELYS_DATALOG_TOKEN_PLUS,
    MAELYS_DATALOG_TOKEN_MINUS,
    MAELYS_DATALOG_TOKEN_STAR
} maelys_datalog_token_kind_t;

typedef struct {
    maelys_datalog_token_kind_t kind;
    char text[MAELYS_DATALOG_MAX_TOKEN_BYTES + 1];
    size_t len;
    long long integer;
    int boolean;
    size_t line;
    size_t column;
} maelys_datalog_token_t;

typedef struct {
    const char *src;
    size_t len;
    size_t pos;
    size_t line;
    size_t column;
    maelys_result_t error;
    const char *file_path;
    maelys_datalog_diagnostic_t *diag;
} maelys_datalog_lexer_t;

maelys_result_t maelys_datalog_lexer_init(maelys_datalog_lexer_t *lexer,
                                          const char *src,
                                          size_t len);
maelys_result_t maelys_datalog_lexer_init_ex(maelys_datalog_lexer_t *lexer,
                                             const char *src,
                                             size_t len,
                                             const char *file_path,
                                             maelys_datalog_diagnostic_t *out_diag);
maelys_result_t maelys_datalog_lexer_next(maelys_datalog_lexer_t *lexer,
                                          maelys_datalog_token_t *out);
maelys_result_t maelys_datalog_lexer_validate(const char *src, size_t len);
maelys_result_t maelys_datalog_lexer_validate_ex(const char *src,
                                                 size_t len,
                                                 const char *file_path,
                                                 maelys_datalog_diagnostic_t *out_diag);

#endif
