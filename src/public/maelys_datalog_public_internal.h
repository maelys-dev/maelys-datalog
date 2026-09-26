/* SPDX-License-Identifier: MPL-2.0 */
#ifndef MAELYS_DATALOG_PUBLIC_INTERNAL_H
#define MAELYS_DATALOG_PUBLIC_INTERNAL_H
#include "maelys/datalog_backend.h"
#include "src/manifest/maelys_datalog_manifest.h"
#include <stdatomic.h>
struct maelys_datalog_policy {
    maelys_datalog_internal_policy_set_t set;
    int owns_storage;
    int released;
    atomic_size_t references;
};
/* Owned immutable storage may outlive the public handle. Caller-owned policy
 * storage is never retained by a session. */
void maelys_datalog_policy_retain_storage(const maelys_datalog_policy_t *);
void maelys_datalog_policy_release_storage(const maelys_datalog_policy_t *);
#endif
