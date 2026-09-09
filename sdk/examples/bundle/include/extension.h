/* SPDX-License-Identifier: MPL-2.0 */
#ifndef MAELYS_EXAMPLE_BUNDLE_H
#define MAELYS_EXAMPLE_BUNDLE_H
#include <maelys/datalog_extension.h>
#ifdef __cplusplus
extern "C" {
#endif
/* One package, one frontend and one filter. No implicit registration. */
maelys_datalog_extension_t example_bundle_extension(void);
#ifdef __cplusplus
}
#endif
#endif
