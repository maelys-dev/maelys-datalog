/* SPDX-License-Identifier: MPL-2.0 */
#include "bindings/javascript/native/transport.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifdef NDEBUG
#error "This conformance probe requires assertions"
#endif
#undef malloc
#undef calloc
#undef realloc
#undef free
#undef memset
static size_t calls, live, fail_at;
static int forbidden;
void *maelys_test_malloc(size_t n) { ++calls; if(forbidden || calls==fail_at) return NULL; void *p=malloc(n); if(p) ++live; return p; }
void *maelys_test_calloc(size_t n,size_t s) { ++calls; if(forbidden || calls==fail_at) return NULL; void *p=calloc(n,s); if(p) ++live; return p; }
void *maelys_test_realloc(void *p,size_t n) { ++calls; if(forbidden || calls==fail_at) return NULL; void *q=realloc(p,n); if(q && !p) ++live; return q; }
void maelys_test_free(void *p) { if(p) { ++calls; --live; } free(p); }
void *maelys_test_memset(void *p,int c,size_t n) { return memset(p,c,n); }
int main(void) {
    /* Every partial context allocation must release what it already owns. */
    for(size_t i=1;i<=3;++i) { calls=0; fail_at=i; assert(!maelys_js_create()); assert(live==0); }
    calls=0; fail_at=0;
    maelys_js_context *c=maelys_js_create(); assert(c && calls==3);
    const char text[]="js_alloc\0seed\0allow\0main\0allow(X) :- seed(X).";
    uint32_t domain[]={0,8,2,0,9,4,1,1,14,5,1,6};
    assert(maelys_js_call(c,2,domain,12,text,sizeof(text))==0);
    uint32_t source[]={0,8,20,4,25,(uint32_t)sizeof(text)-26};
    assert(maelys_js_call(c,3,source,6,text,sizeof(text))==0);
    uint32_t policy=maelys_js_words(c)[0];
    uint32_t create_edb[]={policy,2,64};
    assert(maelys_js_call(c,8,create_edb,3,NULL,0)==0);
    uint32_t edb=maelys_js_words(c)[0], prepare[12]={policy};
    assert(maelys_js_call(c,9,prepare,12,NULL,0)==0);
    uint32_t session=maelys_js_words(c)[0];
    calls=0; forbidden=1;
    uint32_t batch[32]={edb,1,9,4,1,2,42,0};
    assert(maelys_js_call(c,13,batch,17,text,sizeof(text))==0);
    /* The first valid fact of a malformed two-fact batch is never committed. */
    batch[1]=2; memcpy(batch+17,batch+2,15*sizeof(uint32_t)); batch[20]=99;
    assert(maelys_js_call(c,13,batch,32,text,sizeof(text))==-1);
    assert(maelys_js_call(c,14,&edb,1,NULL,0)==0 && maelys_js_words(c)[0]==1);
    uint32_t solve[]={session,edb};
    assert(maelys_js_call(c,15,solve,2,NULL,0)==0);
    uint32_t result=maelys_js_words(c)[0], query[16]={result,14,5,1,2,42,0};
    assert(maelys_js_call(c,16,query,16,text,sizeof(text))==0 && maelys_js_words(c)[0]==1);
    uint32_t enumerate[]={result,1,14,5};
    assert(maelys_js_call(c,17,enumerate,4,text,sizeof(text))==0);
    assert(maelys_js_words(c)[0]==1 && maelys_js_words(c)[1]==2 && maelys_js_words(c)[2]==42);
    assert(maelys_js_call(c,15,solve,2,NULL,0)==-13);
    uint32_t release[]={4,result};
    assert(maelys_js_call(c,6,release,2,NULL,0)==0);
    assert(maelys_js_call(c,20,&result,1,NULL,0)==-13);
    uint32_t reset[]={edb,1};
    assert(maelys_js_call(c,12,reset,2,NULL,0)==0);
    assert(maelys_js_call(c,14,&edb,1,NULL,0)==0 && maelys_js_words(c)[0]==0);
    assert(calls==0); forbidden=0;
    maelys_js_destroy(c); assert(live==0);
    puts("PASS: shared JS transport partial initialization, atomic rollback, prepared allocation contract and leases");
}
