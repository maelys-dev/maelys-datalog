/* SPDX-License-Identifier: MPL-2.0 */
#ifndef MAELYS_DATALOG_CLI_READER_H
#define MAELYS_DATALOG_CLI_READER_H

#include <maelys/datalog.h>
#include <maelys/json.h>

typedef struct {
    maelys_json_document_t *document;
    maelys_datalog_domain_t declaration;
    maelys_datalog_predicate_t *predicates;
    const char **atoms;
    const char *path;
} datalog_cli_domain_t;

typedef struct {
    char message[256];
    size_t index, line, column;
    int has_index;
    int parse_error;
    int io_error;
    int internal_error;
} datalog_cli_error_t;

int datalog_cli_domain_read(const char *path, datalog_cli_domain_t *out,
                            datalog_cli_error_t *error);
void datalog_cli_domain_free(datalog_cli_domain_t *domain);
const maelys_datalog_predicate_t *datalog_cli_predicate(
    const datalog_cli_domain_t *domain, const char *name);

typedef struct {
    char *source;
    maelys_datalog_fact_t *facts;
    size_t count;
} datalog_cli_facts_t;
int datalog_cli_facts_read(const char *path, const datalog_cli_domain_t *domain,
                           datalog_cli_facts_t *out, datalog_cli_error_t *error);
int datalog_cli_term_parse(char *text, maelys_datalog_value_t *out);
void datalog_cli_facts_free(datalog_cli_facts_t *facts);

#endif
