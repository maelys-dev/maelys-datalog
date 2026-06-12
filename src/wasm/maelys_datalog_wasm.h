#pragma once
#ifndef MAELYS_DATALOG_WASM_H
#define MAELYS_DATALOG_WASM_H

#include <stdint.h>
#include <stddef.h>

#include "common/maelys_errors.h"

#ifdef __cplusplus
extern "C" {
#endif

maelys_result_t maelys_datalog_wasm_domain_begin(const char *name);
maelys_result_t maelys_datalog_wasm_domain_add_predicate(const char *name,
                                                         unsigned arity,
                                                         unsigned kind_flags);
maelys_result_t maelys_datalog_wasm_domain_commit(void);
maelys_result_t maelys_datalog_wasm_domain_abort(void);

maelys_result_t maelys_datalog_wasm_load_policy(const char *domain_name,
                                                const char *policy_id,
                                                const char *src,
                                                size_t src_len);
uintptr_t maelys_datalog_wasm_ruleset_ptr(void);
const char *maelys_datalog_wasm_last_diag_message(void);
int maelys_datalog_wasm_last_diag_code(void);

#ifdef __cplusplus
}
#endif

#endif
