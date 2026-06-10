#pragma once
#ifndef MAELYS_DATALOG_PARSER_H
#define MAELYS_DATALOG_PARSER_H

#include "policy/maelys_datalog_policy.h"
#include "policy/maelys_datalog_diagnostic.h"

maelys_result_t maelys_datalog_ruleset_init(maelys_datalog_ruleset_t *ruleset,
                                            const char *policy_id,
                                            const char *domain,
                                            const char *sha256,
                                            int test_only);
void maelys_datalog_ruleset_clear(maelys_datalog_ruleset_t *ruleset);
maelys_result_t maelys_datalog_parse_ruleset(maelys_datalog_ruleset_t *ruleset,
                                             const char *src,
                                             size_t len);
maelys_result_t maelys_datalog_parse_ruleset_ex(maelys_datalog_ruleset_t *ruleset,
                                                const char *src,
                                                size_t len,
                                                const char *file_path,
                                                maelys_datalog_diagnostic_t *out_diag);
int maelys_datalog_ruleset_has_allow_all(const maelys_datalog_ruleset_t *ruleset);

#endif
