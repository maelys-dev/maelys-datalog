/* SPDX-License-Identifier: MPL-2.0 */
#ifndef MAELYS_DATALOG_RECYCLE_INTERNAL_H
#define MAELYS_DATALOG_RECYCLE_INTERNAL_H
#include <stddef.h>
/* Internal qualification hooks, not installed or part of the public ABI.
 * Purging never touches live sessions; subsequent releases can refill the slot. */
#if defined(__GNUC__) || defined(__clang__)
__attribute__((visibility("hidden")))
#endif
void maelys_datalog_session_recycle_purge(void);
#if defined(__GNUC__) || defined(__clang__)
__attribute__((visibility("hidden")))
#endif
size_t maelys_datalog_session_recycle_bound(void);
#endif
