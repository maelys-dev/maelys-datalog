/* SPDX-License-Identifier: MPL-2.0 */
#include "transport.h"
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

int main(void) {
    maelys_js_context *c = maelys_js_create(); assert(c);
    assert(maelys_js_call(c, 0, NULL, 0, NULL, 0) == 0);
    assert(maelys_js_words(c)[0] == 1);
    const char text[] = "domain\0seed\0allow\0main\0allow(X) :- seed(X).";
    uint32_t domain[] = {0,6,2,0,7,4,1,1,12,5,1,6};
    assert(maelys_js_call(c,2,domain,12,text,sizeof(text)) == 0);
    uint32_t source[] = {0,6,18,4,23,(uint32_t)sizeof(text)-24};
    assert(maelys_js_call(c,3,source,6,text,sizeof(text)) == 0);
    uint32_t policy = maelys_js_words(c)[0];
    uint32_t create_edb[] = {policy,2,64};
    assert(maelys_js_call(c,8,create_edb,3,NULL,0) == 0);
    uint32_t edb = maelys_js_words(c)[0];
    uint32_t append[17] = {edb,1,7,4,1,2,42,0};
    assert(maelys_js_call(c,13,append,17,text,sizeof(text)) == 0);
    append[5] = 99;
    assert(maelys_js_call(c,13,append,17,text,sizeof(text)) == -1);
    assert(maelys_js_call(c,14,&edb,1,NULL,0) == 0);
    assert(maelys_js_words(c)[0] == 1);
    uint32_t prepare[12] = {policy};
    assert(maelys_js_call(c,9,prepare,12,NULL,0) == 0);
    uint32_t session = maelys_js_words(c)[0];
    uint32_t solve[] = {session,edb};
    assert(maelys_js_call(c,15,solve,2,NULL,0) == 0);
    uint32_t result = maelys_js_words(c)[0];
    assert(maelys_js_call(c,15,solve,2,NULL,0) == -13);
    uint32_t close_result[] = {4,result};
    assert(maelys_js_call(c,6,close_result,2,NULL,0) == 0);
    assert(maelys_js_call(c,20,&result,1,NULL,0) == -13);
    assert(maelys_js_call(c,15,solve,2,NULL,0) == 0);
    assert(maelys_js_words(c)[0] != result);
    uint32_t close_policy[] = {1,policy};
    assert(maelys_js_call(c,6,close_policy,2,NULL,0) == 0);
    assert(maelys_js_call(c,14,&edb,1,NULL,0) == -13);
    assert(maelys_js_call(c,15,solve,2,NULL,0) == -13);
    /* Malformed frame lengths/offsets/kinds cannot reach freed/foreign handles.
     * ASan/UBSan instruments both this adapter and the engine archive. */
    uint32_t random[64], seed = 0x419fe41u;
    for (unsigned i = 0; i < 20000; ++i) {
        for (unsigned j = 0; j < 64; ++j) { seed ^= seed << 13; seed ^= seed >> 17; seed ^= seed << 5; random[j] = seed; }
        (void)maelys_js_call(c, i % 24, random, i % 64, text, i % sizeof(text));
    }
    maelys_js_destroy(c);
    puts("shared transport malformed frames and ownership PASS");
    return 0;
}
