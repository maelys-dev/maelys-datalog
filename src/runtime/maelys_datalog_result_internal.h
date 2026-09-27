/* SPDX-License-Identifier: MPL-2.0 */
#pragma once
#include "maelys/datalog_backend.h"
#include "src/core/maelys_datalog_types.h"

/* Trusted reference adapter only. Attach an already finalized native view when
 * the selected solve callback borrows inputs. Return zero for an external
 * wrapper, which must keep emitting through the ordinary backend contract.
 * The native result lease owns the facts; the runtime never modifies them. */
int maelys_datalog_backend_borrow_derived(
    maelys_datalog_backend_output_t *, const maelys_datalog_fact_set_t *);
