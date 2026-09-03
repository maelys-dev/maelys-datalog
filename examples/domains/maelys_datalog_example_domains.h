#pragma once
#ifndef MAELYS_DATALOG_EXAMPLE_DOMAINS_H
#define MAELYS_DATALOG_EXAMPLE_DOMAINS_H

#include "include/maelys_datalog.h"

/*
 * Register built-in example domains into the global domain registry.
 *
 * Domains registered:
 *   "graph"    : edge/2 (EDB), source/1 (POLICY_FACT),
 *                path/2 (IDB), reachable/1 (IDB|QUERY)
 *   "decision" : safe/1 (POLICY_FACT), blocked/1 (EDB),
 *                allow/1 (IDB|QUERY), deny/1 (IDB|QUERY),
 *                reduce/1 (IDB|QUERY)
 *
 * Call once before any manifest_load_from_text() that references
 * domain "graph" or domain "decision".
 *
 * Idempotent contract:
 *   - repeated calls return MAELYS_OK if already registered identically;
 *   - no duplicate predicate registration;
 *   - no registry corruption on repeated calls.
 */
maelys_result_t maelys_datalog_example_domains_install(void);

#endif
