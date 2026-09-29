/* SPDX-License-Identifier: MPL-2.0 */
#include "tests/fixtures/allocation_guard.h"
#undef malloc
#undef calloc
#undef realloc
#undef free
#undef memset
#include "maelys/datalog.h"
#include <assert.h>
#include <stdlib.h>
#include <string.h>
static size_t live;
static void (*observe)(size_t);
void *maelys_test_malloc(size_t n) {
    void *p=malloc(n); if(p) { ++live; memset(p,0xa5,n); } return p;
}
void *maelys_test_calloc(size_t n,size_t w) {
    void *p=maelys_test_malloc(n*w); if(p) memset(p,0,n*w); return p;
}
void *maelys_test_realloc(void *p,size_t n) { (void)p;(void)n;abort(); }
void maelys_test_free(void *p) {
    if(p) { assert(live); --live; free(p); if(observe) observe(live); }
}
void *maelys_test_memset(void *p,int c,size_t n) {return memset(p,c,n);}
int exercise_recycle(void (*callback)(size_t)) {
    observe=callback;
    const maelys_datalog_predicate_t ps[]={MAELYS_DATALOG_EDB("seed",1),MAELYS_DATALOG_IDB_QUERY("seen",1)};
    const maelys_datalog_domain_t d={"unload",ps,2,NULL,0};
    assert(!maelys_datalog_domain_register(&d));
    const char text[]="seen(X) :- seed(X).";
    maelys_datalog_policy_t *p;maelys_datalog_session_t *s;
    assert(!maelys_datalog_policy_load_inline("unload","p",text,sizeof(text)-1,&p,NULL));
    assert(!maelys_datalog_session_create(p,0,&s));
    assert(!maelys_datalog_session_free(s));
    assert(!maelys_datalog_policy_free(p));
    callback(live); return live==1 ? 0:1;
}
