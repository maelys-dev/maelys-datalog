/* SPDX-License-Identifier: MPL-2.0 */
#include <maelys/datalog_extension.h>
/* This translation unit MUST fail: consumers may only hold handle pointers. */
int main(void) { return (int)sizeof(OPAQUE_HANDLE); }
