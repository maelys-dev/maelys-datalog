/* SPDX-License-Identifier: MPL-2.0 */
#include "reader.h"

#include <ctype.h>
#include <errno.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
    char *base, *at;
    size_t line, column;
} scanner_t;

static void advance(scanner_t *s) {
    if (*s->at == '\n') { ++s->line; s->column = 1; }
    else ++s->column;
    ++s->at;
}

static int fail(datalog_cli_error_t *error, scanner_t *s, size_t index,
                const char *message) {
    (void)snprintf(error->message, sizeof(error->message),
                   "fact %zu at %zu:%zu: %s", index, s->line, s->column,
                   message);
    error->index = index;
    error->has_index = 1;
    error->line = s->line;
    error->column = s->column;
    return -1;
}

static int valid_utf8(const unsigned char *text, size_t size) {
    for (size_t i = 0; i < size;) {
        unsigned char c = text[i++];
        if (c < 0x80) continue;
        size_t rest;
        if (c >= 0xC2 && c <= 0xDF) rest = 1;
        else if (c >= 0xE0 && c <= 0xEF) rest = 2;
        else if (c >= 0xF0 && c <= 0xF4) rest = 3;
        else return 0;
        if (rest > size - i) return 0;
        if ((text[i] & 0xC0) != 0x80) return 0;
        if ((c == 0xE0 && text[i] < 0xA0) ||
            (c == 0xED && text[i] >= 0xA0) ||
            (c == 0xF0 && text[i] < 0x90) ||
            (c == 0xF4 && text[i] >= 0x90)) return 0;
        for (size_t j = 1; j < rest; ++j)
            if ((text[i + j] & 0xC0) != 0x80) return 0;
        i += rest;
    }
    return 1;
}

static int space(scanner_t *s, datalog_cli_error_t *error, size_t index) {
    for (;;) {
        while (*s->at && isspace((unsigned char)*s->at)) advance(s);
        if (*s->at == '%') {
            while (*s->at && *s->at != '\n') advance(s);
        } else if (s->at[0] == '/' && s->at[1] == '*') {
            advance(s); advance(s);
            while (*s->at && !(s->at[0] == '*' && s->at[1] == '/')) advance(s);
            if (!*s->at) return fail(error, s, index, "unterminated comment");
            advance(s); advance(s);
        } else break;
    }
    return 0;
}

static int identifier(scanner_t *s, char **out) {
    if (!((*s->at >= 'a' && *s->at <= 'z') || *s->at == '_')) return -1;
    *out = s->at;
    while ((*s->at >= 'a' && *s->at <= 'z') ||
           (*s->at >= 'A' && *s->at <= 'Z') ||
           (*s->at >= '0' && *s->at <= '9') || *s->at == '_')
        advance(s);
    return 0;
}

static int value(scanner_t *s, maelys_datalog_value_t *out,
                 datalog_cli_error_t *error, size_t index) {
    if (*s->at == '"') {
        advance(s);
        out->kind = MAELYS_DATALOG_VALUE_SYMBOL;
        out->as.symbol = s->at;
        while (*s->at && *s->at != '"') {
            if (*s->at == '\\' || (unsigned char)*s->at < 0x20)
                return fail(error, s, index, "invalid symbol string");
            advance(s);
        }
        if (!*s->at) return fail(error, s, index, "unterminated symbol string");
        *s->at = 0; advance(s);
        return 0;
    }
    if (*s->at == '-' || (*s->at >= '0' && *s->at <= '9')) {
        char *end;
        errno = 0;
        long long parsed = strtoll(s->at, &end, 10);
        if (end == s->at || errno == ERANGE || parsed < INT64_MIN || parsed > INT64_MAX)
            return fail(error, s, index, "integer outside int64 range");
        out->kind = MAELYS_DATALOG_VALUE_INTEGER;
        out->as.integer = (int64_t)parsed;
        while (s->at < end) advance(s);
        return 0;
    }
    if (strncmp(s->at, "true", 4) == 0 &&
        !(isalnum((unsigned char)s->at[4]) || s->at[4] == '_')) {
        out->kind = MAELYS_DATALOG_VALUE_BOOLEAN;
        out->as.boolean = 1;
        for (int i = 0; i < 4; ++i) advance(s);
        return 0;
    }
    if (strncmp(s->at, "false", 5) == 0 &&
        !(isalnum((unsigned char)s->at[5]) || s->at[5] == '_')) {
        out->kind = MAELYS_DATALOG_VALUE_BOOLEAN;
        out->as.boolean = 0;
        for (int i = 0; i < 5; ++i) advance(s);
        return 0;
    }
    return fail(error, s, index, "expected a quoted symbol, int64 or boolean");
}

/* No escapes are accepted by the source language's quoted symbols. */
int datalog_cli_term_parse(char *text, maelys_datalog_value_t *out) {
    scanner_t s = {text, text, 1, 1};
    datalog_cli_error_t error = {0};
    if (value(&s, out, &error, 0) != 0) return -1;
    return *s.at == 0 ? 0 : -1;
}

static int read_file(const char *path, char **out, datalog_cli_error_t *error) {
    FILE *file = fopen(path, "rb");
    if (!file) {
        error->io_error = 1;
        (void)snprintf(error->message, sizeof(error->message),
                       "cannot open facts: %s", path);
        return -1;
    }
    const size_t limit = 16u * 1024u * 1024u;
    size_t size = 0, capacity = 4096;
    char *buffer = malloc(capacity + 1u);
    if (!buffer) {
        error->internal_error = 1;
        (void)snprintf(error->message, sizeof(error->message), "out of memory");
        fclose(file); return -1;
    }
    for (;;) {
        if (size == capacity) {
            if (capacity >= limit) {
                int next = fgetc(file);
                if (next != EOF || ferror(file)) {
                    error->io_error = ferror(file) != 0;
                    (void)snprintf(error->message, sizeof(error->message),
                                   error->io_error ? "cannot read facts: %s" :
                                                     "facts file exceeds 16 MiB", path);
                    free(buffer); fclose(file); return -1;
                }
                break;
            }
            capacity *= 2u;
            char *grown = realloc(buffer, capacity + 1u);
            if (!grown) {
                error->internal_error = 1;
                (void)snprintf(error->message, sizeof(error->message), "out of memory");
                free(buffer); fclose(file); return -1;
            }
            buffer = grown;
        }
        size_t n = fread(buffer + size, 1u, capacity - size, file);
        size += n;
        if (n == 0) {
            if (ferror(file)) {
                error->io_error = 1;
                (void)snprintf(error->message, sizeof(error->message),
                               "cannot read facts: %s", path);
                free(buffer); fclose(file); return -1;
            }
            break;
        }
    }
    fclose(file);
    if (memchr(buffer, 0, size)) {
        (void)snprintf(error->message, sizeof(error->message),
                       "NUL byte in facts file");
        free(buffer); return -1;
    }
    if (!valid_utf8((const unsigned char *)buffer, size)) {
        (void)snprintf(error->message, sizeof(error->message),
                       "invalid UTF-8 in facts file");
        free(buffer); return -1;
    }
    buffer[size] = 0;
    *out = buffer;
    return 0;
}

int datalog_cli_facts_read(const char *path, const datalog_cli_domain_t *domain,
                           datalog_cli_facts_t *out, datalog_cli_error_t *error) {
    memset(out, 0, sizeof(*out));
    memset(error, 0, sizeof(*error));
    if (read_file(path, &out->source, error) != 0) return -1;
    size_t capacity;
    if (maelys_datalog_limit_get(MAELYS_DATALOG_LIMIT_MAX_EDB_FACTS,
                                &capacity) != MAELYS_DATALOG_STATUS_OK) {
        error->internal_error = 1;
        (void)snprintf(error->message, sizeof(error->message),
                       "cannot read EDB fact limit");
        return -1;
    }
    out->facts = calloc(capacity + 1u, sizeof(*out->facts));
    if (!out->facts) {
        error->internal_error = 1;
        (void)snprintf(error->message, sizeof(error->message), "out of memory");
        return -1;
    }
    scanner_t s = {out->source, out->source, 1, 1};
    for (;;) {
        if (space(&s, error, out->count) != 0) return -1;
        if (!*s.at) break;
        if (out->count == capacity)
            return fail(error, &s, out->count, "too many facts");
        maelys_datalog_fact_t *fact = &out->facts[out->count];
        char *name;
        if (identifier(&s, &name) != 0)
            return fail(error, &s, out->count, "expected predicate name");
        char delimiter = *s.at;
        *s.at = 0;
        const maelys_datalog_predicate_t *predicate = datalog_cli_predicate(domain, name);
        if (!predicate || !(predicate->flags & MAELYS_DATALOG_PREDICATE_EDB))
            return fail(error, &s, out->count, "unknown or non-EDB predicate");
        fact->predicate = predicate->name;
        *s.at = delimiter;
        if (space(&s, error, out->count) != 0) return -1;
        if (*s.at == '(') {
            advance(&s);
            if (space(&s, error, out->count) != 0) return -1;
            if (*s.at != ')') for (;;) {
                if (fact->arity == MAELYS_DATALOG_PUBLIC_MAX_TERMS)
                    return fail(error, &s, out->count, "fact arity exceeds engine limit");
                if (value(&s, &fact->terms[fact->arity], error, out->count) != 0)
                    return -1;
                ++fact->arity;
                if (space(&s, error, out->count) != 0) return -1;
                if (*s.at != ',') break;
                advance(&s);
                if (space(&s, error, out->count) != 0) return -1;
            }
            if (*s.at != ')') return fail(error, &s, out->count, "expected ')'");
            advance(&s);
        }
        if (fact->arity != predicate->arity)
            return fail(error, &s, out->count, "fact arity differs from domain");
        if (space(&s, error, out->count) != 0) return -1;
        if (*s.at != '.') return fail(error, &s, out->count, "expected '.'");
        advance(&s);
        ++out->count;
    }
    return 0;
}

void datalog_cli_facts_free(datalog_cli_facts_t *facts) {
    free(facts->facts);
    free(facts->source);
    memset(facts, 0, sizeof(*facts));
}
