#pragma once

/* Repository-private historical aggregation surface.
 *
 * Not installed or shipped in SDK archives since 0.10.0. Internal tests and
 * benchmarks may use it; application and extension code includes maelys/.
 * Use <maelys/datalog.h> and, for advanced operations on the same handles,
 * <maelys/datalog_advanced.h>. Implementation types are not an SDK contract.
 */

/* Version macros live in the generated header (single source: VERSION).
 * Regenerate with scripts/generate-version-header.sh; never edit either
 * copy by hand — `make check-version-header` enforces the pairing. */
#include "maelys_datalog_version.h"

#include "src/core/maelys_datalog_audit.h"
#include "src/core/maelys_datalog_decision.h"
#include "src/core/maelys_datalog_domain_registry.h"
#include "src/core/maelys_datalog_edb.h"
#include "src/core/maelys_datalog_explanation_format.h"
#include "src/manifest/maelys_datalog_manifest.h"
#include "src/core/maelys_datalog_parser.h"
#include "src/core/maelys_datalog_prepared_session.h"
#include "src/core/maelys_datalog_predicate_registry.h"
#include "src/core/maelys_datalog_ruleset.h"
#include "src/core/maelys_datalog_solver.h"
#include "src/core/maelys_datalog_symbol_table.h"
#include "src/core/maelys_datalog_types.h"
