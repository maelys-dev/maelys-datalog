/* SPDX-License-Identifier: MIT */
#ifndef ALLOCATION_PROVIDER_FIXTURE_H
#define ALLOCATION_PROVIDER_FIXTURE_H
#include <maelys/datalog_backend.h>
/* Bounded public-only conformance fixture, NOT a general Datalog backend.
 * Admits <=8 identity-projection rules over EDB, optionally with one EDB anti-join using exactly the same variables; arity <=4, no constants or
 * repeated variables, no compiled facts; input text <=63 bytes (otherwise an
 * explicit solve rejection). No reference-solver call. Two prepare blocks,
 * one candidate plus temporary block per solve; anti-join output uses a separate
 * block sized to its result, including growth after blocker withdrawal; old block lives until accepted
 * result cleanup. Scratch retained before solve is restored to zero on abort. */
const maelys_datalog_backend_v6_t *allocation_fixture_snapshot(void);
const maelys_datalog_backend_v7_t *allocation_fixture_transactions(void);
typedef struct {
    maelys_datalog_input_base_t base;
    size_t commits, aborts, releases, snapshots, deltas, delivered, retained;
    uint64_t digest;
    size_t output_bytes;
} allocation_fixture_observation;
void allocation_fixture_observe(const void *storage,allocation_fixture_observation *);
/* Test-only failure injection: 1 callback failure, 2 ignored emit failure,
 * 3 ignored work failure, 4 swallowed acquisition failure,
 * 5 abort with an unreleased provisional block (host must reclaim it). Controls/counters are not committed payload. */
/* Exact committed rows/base image for rollback witnesses; counters and scratch excluded. */
size_t allocation_fixture_committed(const void *storage,void *out,size_t capacity);
/* Whole provider arena except test controls/counters. Live block bytes remain
 * the caller ledger's responsibility; neither image is the host session. */
int allocation_fixture_scratch_valid(const void *);
size_t allocation_fixture_arena_image(const void *,void *,size_t);
void allocation_fixture_fault(void *storage,unsigned mode);
void allocation_fixture_hook(void *,
    void (*)(void *,const maelys_datalog_allocation_service_t *),void *);
void allocation_fixture_service(const void *,maelys_datalog_allocation_service_t *);
const void *allocation_fixture_live(const void *storage);
#endif
