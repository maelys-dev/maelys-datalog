/* SPDX-License-Identifier: MPL-2.0 */
#ifndef MAELYS_JS_TRANSPORT_H
#define MAELYS_JS_TRANSPORT_H
#include <stdint.h>
typedef struct maelys_js_context maelys_js_context;
/* Private binding wire ABI; never exposes native pointers or public C layouts. */
maelys_js_context *maelys_js_create(void);
void maelys_js_destroy(maelys_js_context *);
int maelys_js_call(maelys_js_context *, uint32_t, const uint32_t *, uint32_t, const char *, uint32_t);
const uint32_t *maelys_js_words(maelys_js_context *);
uint32_t maelys_js_word_count(maelys_js_context *);
const char *maelys_js_text(maelys_js_context *);
uint32_t maelys_js_scalar(maelys_js_context *, uint32_t);
const char *maelys_js_diagnostic(maelys_js_context *, uint32_t);
#endif
