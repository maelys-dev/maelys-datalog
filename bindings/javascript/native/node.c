/* SPDX-License-Identifier: MPL-2.0 */
#define NAPI_VERSION 8
#include <node_api.h>
#include <pthread.h>
#include <stdlib.h>
#include "transport.h"

/* Registry state belongs to the library, including across Node workers. Serialize
 * every entry; handles themselves never cross an environment or JS Engine. */
static pthread_mutex_t lock;
static pthread_once_t once = PTHREAD_ONCE_INIT;
static void init_lock(void) {
    pthread_mutexattr_t attr;
    pthread_mutexattr_init(&attr);
    pthread_mutexattr_settype(&attr, PTHREAD_MUTEX_RECURSIVE);
    pthread_mutex_init(&lock, &attr);
    pthread_mutexattr_destroy(&attr);
}
static const napi_type_tag tag = { UINT64_C(0x39f8841bb8ba498b), UINT64_C(0xae771644b6808e70) };
typedef struct { maelys_js_context *context; } owner;
static void finalize(napi_env env, void *data, void *hint) {
    (void)env; (void)hint;
    owner *o = data;
    pthread_mutex_lock(&lock); maelys_js_destroy(o->context); pthread_mutex_unlock(&lock);
    free(o);
}
static napi_value error(napi_env env, const char *message) {
    napi_throw_error(env, NULL, message); return NULL;
}
#define NAPI(expr) do { if ((expr) != napi_ok) return error(env, "Node-API binding failure"); } while (0)
static owner *receiver(napi_env env, napi_value self) {
    bool valid = false; owner *o = NULL;
    if (napi_check_object_type_tag(env, self, &tag, &valid) != napi_ok || !valid ||
        napi_unwrap(env, self, (void **)&o) != napi_ok) return NULL;
    return o;
}
static napi_value close_context(napi_env env, napi_callback_info info) {
    napi_value self, undefined; size_t count = 0;
    NAPI(napi_get_cb_info(env, info, &count, NULL, &self, NULL));
    owner *o = receiver(env, self);
    if (!o) return error(env, "Invalid native receiver");
    pthread_mutex_lock(&lock); maelys_js_destroy(o->context); o->context = NULL; pthread_mutex_unlock(&lock);
    NAPI(napi_get_undefined(env, &undefined)); return undefined;
}
static napi_status response(napi_env env, maelys_js_context *c, int status, napi_value *out) {
    napi_value words, buffer, value, scalars, texts;
    uint32_t *copy;
#define PUT(expr) do { napi_status s = (expr); if (s != napi_ok) return s; } while (0)
    PUT(napi_create_object(env, out));
    PUT(napi_create_int32(env, status, &value)); PUT(napi_set_named_property(env, *out, "status", value));
    uint32_t n = maelys_js_word_count(c);
    PUT(napi_create_arraybuffer(env, (size_t)n * 4, (void **)&copy, &buffer));
    for (uint32_t i = 0; i < n; ++i) copy[i] = maelys_js_words(c)[i];
    PUT(napi_create_typedarray(env, napi_uint32_array, n, buffer, 0, &words));
    PUT(napi_set_named_property(env, *out, "words", words));
    PUT(napi_create_string_utf8(env, maelys_js_text(c), NAPI_AUTO_LENGTH, &value));
    PUT(napi_set_named_property(env, *out, "text", value));
    if (status) {
        PUT(napi_create_array_with_length(env, 24, &scalars));
        for (uint32_t i = 0; i < 24; ++i) {
            PUT(napi_create_uint32(env, maelys_js_scalar(c, i), &value));
            PUT(napi_set_element(env, scalars, i, value));
        }
        PUT(napi_create_array_with_length(env, 10, &texts));
        for (uint32_t i = 0; i < 10; ++i) {
            const char *t = maelys_js_diagnostic(c, i);
            PUT(napi_create_string_utf8(env, t ? t : "", NAPI_AUTO_LENGTH, &value));
            PUT(napi_set_element(env, texts, i, value));
        }
        PUT(napi_set_named_property(env, *out, "scalars", scalars));
        PUT(napi_set_named_property(env, *out, "texts", texts));
    }
#undef PUT
    return napi_ok;
}
static napi_value call(napi_env env, napi_callback_info info) {
    napi_value args[3], self, words_buffer, text_buffer, result;
    size_t count = 3, n, bytes, offset;
    uint32_t op; void *words, *text; napi_typedarray_type type;
    NAPI(napi_get_cb_info(env, info, &count, args, &self, NULL));
    owner *o = receiver(env, self);
    if (!o || !o->context || count != 3) return error(env, "Invalid or closed native receiver");
    NAPI(napi_get_value_uint32(env, args[0], &op));
    NAPI(napi_get_typedarray_info(env, args[1], &type, &n, &words, &words_buffer, &offset));
    if (type != napi_uint32_array || n > UINT32_MAX) return error(env, "Expected uint32 words");
    NAPI(napi_get_typedarray_info(env, args[2], &type, &bytes, &text, &text_buffer, &offset));
    if (type != napi_uint8_array || bytes > UINT32_MAX) return error(env, "Expected byte text");
    bool regular_words = false, regular_text = false;
    NAPI(napi_is_arraybuffer(env, words_buffer, &regular_words));
    NAPI(napi_is_arraybuffer(env, text_buffer, &regular_text));
    if (!regular_words || !regular_text) return error(env, "Shared buffers are not accepted");
    pthread_mutex_lock(&lock);
    int status = maelys_js_call(o->context, op, words, (uint32_t)n, text, (uint32_t)bytes);
    napi_status rc = response(env, o->context, status, &result);
    pthread_mutex_unlock(&lock);
    NAPI(rc); return result;
}
static napi_value create(napi_env env, napi_callback_info info) {
    (void)info;
    napi_value object;
    NAPI(napi_create_object(env, &object));
    NAPI(napi_type_tag_object(env, object, &tag));
    owner *o = calloc(1, sizeof(*o));
    if (!o) return error(env, "Native allocation failed");
    pthread_mutex_lock(&lock); o->context = maelys_js_create(); pthread_mutex_unlock(&lock);
    if (!o->context) { free(o); return error(env, "Engine initialization failed"); }
    if (napi_wrap(env, object, o, finalize, NULL, NULL) != napi_ok) {
        finalize(env, o, NULL); return error(env, "Native owner initialization failed");
    }
    const napi_property_descriptor methods[] = {
        {"call", NULL, call, NULL, NULL, NULL, napi_default, NULL},
        {"close", NULL, close_context, NULL, NULL, NULL, napi_default, NULL}
    };
    NAPI(napi_define_properties(env, object, 2, methods)); return object;
}
NAPI_MODULE_INIT() {
    pthread_once(&once, init_lock);
    napi_value fn;
    NAPI(napi_create_function(env, "create", NAPI_AUTO_LENGTH, create, NULL, &fn));
    NAPI(napi_set_named_property(env, exports, "create", fn)); return exports;
}
