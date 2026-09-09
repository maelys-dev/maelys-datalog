/* SPDX-License-Identifier: MPL-2.0 */
/* Small authorization DSL: permit "alice"
 * lowers to allow(X) :- candidate(X), exact_match(X, "alice").
 * This file is self-contained so the example can be copied outside the repo. */
#include "extension.h"
#include <stdio.h>
#include <string.h>

#define MATCH_NAME "exact_match"
#define MATCH_ID "example.exact-match.bytes-v1"

static maelys_datalog_status_t validate(const unsigned char *pattern, size_t n) {
    return pattern || !n ? MAELYS_DATALOG_STATUS_OK : MAELYS_DATALOG_STATUS_INVALID_ARGUMENT;
}
static maelys_datalog_status_t cost(size_t value_length, size_t pattern_length, size_t *out) {
    (void)value_length;
    if (!out)
        return MAELYS_DATALOG_STATUS_INVALID_ARGUMENT;
    if (pattern_length == SIZE_MAX)
        return MAELYS_DATALOG_STATUS_PAYLOAD_TOO_LARGE;
    *out = pattern_length + 1u;
    return MAELYS_DATALOG_STATUS_OK;
}
static maelys_datalog_status_t evaluate(const unsigned char *value, size_t vn,
                                        const unsigned char *pattern, size_t pn, int *out) {
    if (!out || (!value && vn) || (!pattern && pn))
        return MAELYS_DATALOG_STATUS_INVALID_ARGUMENT;
    *out = vn == pn && (!pn || !memcmp(value, pattern, pn));
    return MAELYS_DATALOG_STATUS_OK;
}
static const maelys_datalog_filter_module_t match_filter = {MAELYS_DATALOG_MODULE_ABI_VERSION,
                                                            sizeof(maelys_datalog_filter_module_t),
                                                            MATCH_NAME,
                                                            MATCH_ID,
                                                            validate,
                                                            cost,
                                                            evaluate};

static void spaces(const char *s, size_t end, size_t *i) {
    while (*i < end && (s[*i] == ' ' || s[*i] == '\t' || s[*i] == '\r'))
        ++*i;
}
static maelys_datalog_status_t failure(maelys_datalog_public_diagnostic_t *diag,
                                       maelys_datalog_status_t rc, size_t line, size_t column,
                                       const char *message) {
    if (diag) {
        memset(diag, 0, sizeof(*diag));
        diag->source = MAELYS_DATALOG_DIAGNOSTIC_LOAD;
        diag->code = rc;
        diag->line = line;
        diag->column = column;
        snprintf(diag->phase, sizeof(diag->phase), "permit");
        snprintf(diag->message, sizeof(diag->message), "%s", message);
    }
    return rc;
}
/* Source slices are borrowed only until add_rule copies them. The frontend
 * names AND pins the filter semantics; it never calls evaluate directly. */
static maelys_datalog_status_t emit(maelys_datalog_program_builder_t *builder, const char *pattern,
                                    size_t length, size_t line, size_t column) {
    maelys_datalog_ir_term_t subject = {0};
    subject.kind = MAELYS_DATALOG_IR_VARIABLE;
    subject.as.variable = 0;
    maelys_datalog_ir_rule_t rule = {0};
    rule.head.predicate = "allow";
    rule.head.arity = 1;
    rule.head.terms[0] = subject;
    rule.body_count = 2;
    rule.body[0].kind = MAELYS_DATALOG_IR_ATOM;
    rule.body[0].atom.predicate = "candidate";
    rule.body[0].atom.arity = 1;
    rule.body[0].atom.terms[0] = subject;
    rule.body[1].kind = MAELYS_DATALOG_IR_FILTER;
    rule.body[1].filter_name = MATCH_NAME;
    rule.body[1].filter_semantic_id = MATCH_ID;
    rule.body[1].filter_value = subject;
    rule.body[1].pattern = (const unsigned char *)pattern;
    rule.body[1].pattern_length = length;
    rule.source = (maelys_datalog_source_location_t){line, column};
    return maelys_datalog_program_add_rule(builder, &rule);
}
static maelys_datalog_status_t lower(const char *source, size_t length,
                                     maelys_datalog_program_builder_t *builder,
                                     maelys_datalog_public_diagnostic_t *diag) {
    if (!source || !builder)
        return MAELYS_DATALOG_STATUS_INVALID_ARGUMENT;
    size_t begin = 0, line = 1;
    while (begin < length) {
        size_t end = begin;
        while (end < length && source[end] != '\n')
            ++end;
        size_t i = begin;
        spaces(source, end, &i);
        if (i < end && source[i] != '#') {
            const size_t column = i - begin + 1;
            if (end - i < 6 || memcmp(source + i, "permit", 6))
                goto syntax;
            i += 6;
            if (i == end || (source[i] != ' ' && source[i] != '\t'))
                goto syntax;
            spaces(source, end, &i);
            if (i == end || source[i] != '"')
                goto syntax;
            const size_t start = ++i;
            while (i < end && source[i] != '"') {
                if ((unsigned char)source[i] < 0x20 || source[i] == '\\')
                    goto syntax;
                ++i;
            }
            if (i == end)
                goto syntax;
            const size_t size = i - start;
            ++i;
            spaces(source, end, &i);
            if (i != end)
                goto syntax;
            maelys_datalog_status_t rc = emit(builder, source + start, size, line, column);
            if (rc)
                return failure(
                    diag, rc, line, column,
                    "cannot lower permit rule: check domain, exact_match identity and IR limits");
        }
        begin = end < length ? end + 1 : end;
        ++line;
        continue;
    syntax:
        return failure(diag, MAELYS_DATALOG_STATUS_INVALID_FIELD, line, i - begin + 1,
                       "expected: permit \"account\" (one per line; no escapes or trailing text)");
    }
    return MAELYS_DATALOG_STATUS_OK;
}
static const maelys_datalog_frontend_t permit_frontend = {MAELYS_DATALOG_PROGRAM_ABI_VERSION,
                                                          sizeof(maelys_datalog_frontend_t),
                                                          "permit", "example.permit.v1", lower};
maelys_datalog_extension_t example_bundle_extension(void) {
    maelys_datalog_extension_t e = {0};
    e.abi_version = MAELYS_DATALOG_EXTENSION_ABI_VERSION;
    e.struct_size = sizeof(e);
    e.name = "example_bundle";
    e.semantic_id = "example.bundle.v1";
    e.frontends = &permit_frontend;
    e.frontend_count = 1;
    e.filters = &match_filter;
    e.filter_count = 1;
    return e;
}
