/* SPDX-License-Identifier: MIT */
#ifndef INPUT_PROVIDER_FIXTURE_H
#define INPUT_PROVIDER_FIXTURE_H
#include <maelys/datalog_backend.h>
/* Bounded public-only conformance fixture, NOT a general Datalog backend.
 * Admits <=8 identity-projection rules over EDB, arity <=4, no constants or
 * repeated variables, no compiled facts; input text <=63 bytes (otherwise an
 * explicit solve rejection). No reference-solver call. */
const maelys_datalog_backend_v6_t *input_fixture_snapshot(void);
const maelys_datalog_backend_v7_t *input_fixture_transactions(void);
typedef struct {
    maelys_datalog_input_base_t base;
    size_t commits, aborts, releases, snapshots, deltas, delivered, retained;
    uint64_t digest;
} input_fixture_observation;
void input_fixture_observe(const void *storage,input_fixture_observation *);
/* Test-only failure injection: 1 callback failure, 2 ignored emit failure,
 * 3 ignored work failure. Controls/counters are not committed payload. */
/* Exact committed rows/base image for rollback witnesses; counters and scratch excluded. */
size_t input_fixture_committed(const void *storage,void *out,size_t capacity);
void input_fixture_fault(void *storage,unsigned mode);
#endif
