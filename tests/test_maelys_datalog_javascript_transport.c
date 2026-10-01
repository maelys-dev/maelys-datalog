/* SPDX-License-Identifier: MPL-2.0 */
#include "bindings/javascript/native/transport.h"
#include "src/runtime/maelys_datalog_recycle_internal.h"
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
    /* New attachment storage and staging fail independently without leaks or
     * attaching half an owner. The same session remains usable after each. */
    assert(maelys_js_call(c,9,prepare,12,NULL,0)==0);
    session=maelys_js_words(c)[0];
    uint32_t attach[]={session,2,3,3,1,9,4};
    for(size_t i=1;i<=4;++i) {
        size_t before=live; calls=0; fail_at=i;
        assert(maelys_js_call(c,22,attach,7,text,sizeof(text))==-8);
        assert(live==before);
    }
    fail_at=0;
    assert(maelys_js_call(c,22,attach,7,text,sizeof(text))==0);
    uint32_t input=maelys_js_words(c)[0], tx[37]={input};
    assert(maelys_js_call(c,23,&input,1,NULL,0)==0);
    memcpy(tx+1,maelys_js_words(c),4*sizeof(uint32_t));
    uint32_t original[4];memcpy(original,tx+1,sizeof(original));
    calls=0;forbidden=1;
    tx[5]=1;tx[6]=9;tx[7]=4;tx[8]=1;tx[9]=2;tx[10]=42;
    assert(maelys_js_call(c,24,tx,21,text,sizeof(text))==0);
    result=maelys_js_words(c)[0];
    release[0]=5;release[1]=input;
    assert(maelys_js_call(c,6,release,2,NULL,0)==-13);
    release[0]=4;release[1]=result;
    assert(maelys_js_call(c,6,release,2,NULL,0)==0);
    assert(maelys_js_call(c,24,tx,21,text,sizeof(text))==-13); /* stale */
    assert(maelys_js_call(c,23,&input,1,NULL,0)==0);
    memcpy(tx+1,maelys_js_words(c),4*sizeof(uint32_t));
    assert(tx[1]==original[0] && tx[2]==original[1] && tx[3]==1 && tx[4]==0);
    uint32_t current[4];memcpy(current,tx+1,sizeof(current));
    /* Bad removed record must not publish the valid addition preceding it. */
    memset(tx+5,0,32*sizeof(uint32_t));tx[5]=1;tx[6]=1;
    tx[7]=9;tx[8]=4;tx[9]=1;tx[10]=2;tx[11]=43;
    memcpy(tx+22,tx+7,15*sizeof(uint32_t));tx[25]=99;
    assert(maelys_js_call(c,25,tx,37,text,sizeof(text))==-1);
    assert(maelys_js_call(c,23,&input,1,NULL,0)==0);
    assert(!memcmp(current,maelys_js_words(c),sizeof(current)));
    /* A no-op yields the original committed set and advances the base. */
    tx[5]=tx[6]=0;
    assert(maelys_js_call(c,25,tx,7,NULL,0)==0); result=maelys_js_words(c)[0];
    query[0]=result;query[5]=42;
    assert(maelys_js_call(c,16,query,16,text,sizeof(text))==0 && maelys_js_words(c)[0]==1);
    query[5]=43;
    assert(maelys_js_call(c,16,query,16,text,sizeof(text))==0 && maelys_js_words(c)[0]==0);
    release[1]=result;assert(maelys_js_call(c,6,release,2,NULL,0)==0);
    /* Read/base, delta, queries and release allocate nothing, repeatedly. */
    for(unsigned i=0;i<8;++i) {
        assert(maelys_js_call(c,23,&input,1,NULL,0)==0);
        memcpy(tx+1,maelys_js_words(c),4*sizeof(uint32_t));
        tx[5]=tx[6]=1;tx[25]=2;tx[26]=42;
        assert(maelys_js_call(c,25,tx,37,text,sizeof(text))==0);
        release[1]=maelys_js_words(c)[0];assert(maelys_js_call(c,6,release,2,NULL,0)==0);
    }
    assert(calls==0);forbidden=0;
    /* Destroying an engine with an attachment AND its result must close the
     * result first, regardless of the native handle-list insertion order. */
    assert(maelys_js_call(c,23,&input,1,NULL,0)==0);
    memcpy(tx+1,maelys_js_words(c),4*sizeof(uint32_t));tx[5]=tx[6]=0;
    assert(maelys_js_call(c,25,tx,7,NULL,0)==0);
    maelys_js_destroy(c);
#if (defined(__GNUC__) || defined(__clang__)) && !defined(__EMSCRIPTEN__)
    /* Native execution retains one idle arena; WASM deliberately does not. */
    assert(live==1);
#endif
    maelys_datalog_session_recycle_purge(); assert(live==0);
    puts("PASS: shared JS transport partial initialization, atomic rollback, retained transactions, allocation contract and leases");
}
