/* SPDX-License-Identifier: MPL-2.0 */
#include <maelys/datalog_backend.h>
typedef struct {
    maelys_datalog_fact_t recorded[512];
    size_t recorded_count;
    uint64_t committed_hash, pending_hash;
    unsigned commits, aborts, releases;
    int fail, live, accepted;
    const char *input, *output;
} delta_backend_state;
const maelys_datalog_backend_t *delta_snapshot_backend(void);
