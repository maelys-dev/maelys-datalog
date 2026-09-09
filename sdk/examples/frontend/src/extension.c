/* SPDX-License-Identifier: MPL-2.0 */
/* Demonstration DSL, NOT Gitolite: one unary implication per line,
 * e.g. "allow <- member" means allow(X) :- member(X). */
#include <maelys/datalog_extension.h>
#include <stdio.h>
#include <string.h>

static void spaces(const char *s, size_t end, size_t *i) {
    while (*i < end && (s[*i] == ' ' || s[*i] == '\t' || s[*i] == '\r'))
        ++*i;
}
static int name(const char *s, size_t end, size_t *i, char out[64]) {
    size_t n = 0;
    if (*i == end || s[*i] < 'a' || s[*i] > 'z')
        return 0;
    while (*i < end) {
        char c = s[*i];
        if (!((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_'))
            break;
        if (n == 63u)
            return 0;
        out[n++] = c;
        ++*i;
    }
    out[n] = 0;
    return 1;
}
static maelys_datalog_status_t lower(const char *source, size_t length,
                                     maelys_datalog_program_builder_t *builder,
                                     maelys_datalog_public_diagnostic_t *diag) {
    size_t line = 1, begin = 0;
    while (begin < length) {
        size_t end = begin;
        while (end < length && source[end] != '\n')
            ++end;
        size_t i = begin;
        spaces(source, end, &i);
        if (i != end && source[i] != '#') {
            char head[64], body[64];
            const size_t column = i - begin + 1u;
            int valid = name(source, end, &i, head);
            spaces(source, end, &i);
            if (i + 1u >= end || source[i] != '<' || source[i + 1u] != '-')
                valid = 0;
            if (valid) {
                i += 2u;
                spaces(source, end, &i);
                valid = name(source, end, &i, body);
                spaces(source, end, &i);
                if (i != end)
                    valid = 0;
            }
            if (!valid) {
                if (diag) {
                    memset(diag, 0, sizeof(*diag));
                    diag->source = MAELYS_DATALOG_DIAGNOSTIC_LOAD;
                    diag->code = MAELYS_DATALOG_STATUS_INVALID_FIELD;
                    diag->line = line;
                    diag->column = column;
                    snprintf(diag->phase, sizeof(diag->phase), "arrow");
                    snprintf(diag->message, sizeof(diag->message), "expected: head <- body");
                }
                return MAELYS_DATALOG_STATUS_INVALID_FIELD;
            }
            maelys_datalog_ir_rule_t rule = {0};
            rule.head.predicate = head;
            rule.head.arity = 1u;
            rule.head.terms[0].kind = MAELYS_DATALOG_IR_VARIABLE;
            rule.body_count = 1u;
            rule.body[0].kind = MAELYS_DATALOG_IR_ATOM;
            rule.body[0].atom.predicate = body;
            rule.body[0].atom.arity = 1u;
            rule.body[0].atom.terms[0].kind = MAELYS_DATALOG_IR_VARIABLE;
            rule.source = (maelys_datalog_source_location_t){line, column};
            maelys_datalog_status_t rc = maelys_datalog_program_add_rule(builder, &rule);
            if (rc != MAELYS_DATALOG_STATUS_OK)
                return rc;
        }
        begin = end < length ? end + 1u : end;
        ++line;
    }
    return MAELYS_DATALOG_STATUS_OK;
}
const maelys_datalog_frontend_t *example_arrow_frontend(void) {
    static const maelys_datalog_frontend_t frontend = {MAELYS_DATALOG_PROGRAM_ABI_VERSION,
                                                       sizeof(maelys_datalog_frontend_t), "arrow",
                                                       "example.arrow.v1", lower};
    return &frontend;
}
maelys_datalog_extension_t example_frontend_extension(void) {
    maelys_datalog_extension_t e = {0};
    e.abi_version = MAELYS_DATALOG_EXTENSION_ABI_VERSION;
    e.struct_size = sizeof(e);
    e.name = "example_frontend";
    e.semantic_id = "example.frontend.v1";
    e.frontends = example_arrow_frontend();
    e.frontend_count = 1;
    return e;
}
