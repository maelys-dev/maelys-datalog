/* SPDX-License-Identifier: MPL-2.0 */
/* Public accessor and independent public-IR oracle. Private access is confined
 * to constructing the future-kind probe and borrowing a program without a
 * session. Make/CMake instrument every engine translation unit. */
#undef malloc
#undef calloc
#undef realloc
#undef free
#undef memset
#include <maelys/datalog_program.h>
#include <maelys/datalog_extension.h>
#include <maelys/datalog_resources.h>
#include "src/public/maelys_datalog_public_internal.h"
#include "src/compiler/maelys_datalog_program_internal.h"
#include "tests/helpers/policy_reads_oracle.h"
#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>

#define CHECK(x) do { if (!(x)) { fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #x); return 1; } } while (0)
#define OK(x) CHECK((x) == MAELYS_DATALOG_STATUS_OK)

static int forbid_allocator;
static size_t forbidden_calls;
static int refuse_allocation(void) {
    if (!forbid_allocator) return 0;
    ++forbidden_calls;
    return 1;
}
void *maelys_test_malloc(size_t n) { return refuse_allocation() ? NULL : malloc(n); }
void *maelys_test_calloc(size_t n, size_t s) { return refuse_allocation() ? NULL : calloc(n, s); }
void *maelys_test_realloc(void *p, size_t n) { return refuse_allocation() ? NULL : realloc(p, n); }
void maelys_test_free(void *p) {
    if (forbid_allocator) ++forbidden_calls;
    free(p);
}
void *maelys_test_memset(void *p, int c, size_t n) { return memset(p, c, n); }

static const maelys_datalog_predicate_t defs[]={
    {"seed",1,MAELYS_DATALOG_PREDICATE_EDB}, {"blocked",1,MAELYS_DATALOG_PREDICATE_EDB},
    {"agg",1,MAELYS_DATALOG_PREDICATE_EDB}, {"unused",1,MAELYS_DATALOG_PREDICATE_EDB},
    {"out",1,MAELYS_DATALOG_PREDICATE_IDB|MAELYS_DATALOG_PREDICATE_QUERY},
    {"fixed",1,MAELYS_DATALOG_PREDICATE_POLICY_FACT}};
static int answer(maelys_datalog_policy_t *p,const char *name,size_t arity,int expected) {
    int value=42;
    OK(maelys_datalog_policy_reads_predicate(p,0,name,arity,&value)); CHECK(value==expected); return 0;
}
static int named_cases(void) {
    const char *sources[]={"out(X):-seed(X).", "out(X):-seed(X),not(blocked(X)).",
        "out(N):-count(V,agg(V),N).", "out(N):-min(V,agg(V),N).",
        "out(N):-max(V,agg(V),N).", "out(N):-sum(V,agg(V),N).",
        "out(X):-seed(X),X = X,starts_with(X,\"a\"). fixed(1)."};
    const char *names[]={"positive","negation","count","min","max","sum","comparison_filter_head_fact_excluded"};
    for(size_t i=0;i<7;i++) {
        maelys_datalog_policy_t *p=NULL;
        maelys_datalog_diagnostic_t d=MAELYS_DATALOG_DIAGNOSTIC_INIT;
        int rc=maelys_datalog_policy_load_inline("reads","r",sources[i],strlen(sources[i]),&p,&d);
        if (rc) {
            fprintf(stderr,"%s: %s\n",names[i],d.message);
        }
        OK(rc);
        maelys_datalog_program_t program={.ruleset=&p->set.policies[0]};
        forbid_allocator=1;
        CHECK(!answer(p,"seed",1,i<2 || i==6)); CHECK(!answer(p,"blocked",1,i==1));
        CHECK(!answer(p,"agg",1,i>=2 && i<=5)); CHECK(!answer(p,"unused",1,0));
        CHECK(!answer(p,"out",1,0)); CHECK(!answer(p,"fixed",1,0));
        CHECK(!answer(p,"unknown",1,0)); CHECK(!answer(p,"seed",2,0));
        CHECK(!policy_reads_oracle(p,0,&program)); CHECK(!forbidden_calls);
        forbid_allocator=0;
        OK(maelys_datalog_policy_free(p)); printf("%s PASS\n",names[i]);
    }
    return 0;
}
static int unknown_is_all_registry_pairs(void) {
    maelys_datalog_policy_t *p=NULL;
    const char *source="out(X):-seed(X).";
    OK(maelys_datalog_policy_load_inline("reads","unknown",source,strlen(source),&p,NULL));
    maelys_datalog_internal_ruleset_t *r=&p->set.policies[0];
    /* Force the future-kind path below validation, and add a builtin registry
     * pair that is absent from the domain declaration. No public malformed IR. */
    r->registry.frozen=0;
    OK((int)maelys_datalog_predicate_registry_add_builtin(&r->registry,"builtin_probe",2,MAELYS_DATALOG_PREDICATE_EDB));
    OK((int)maelys_datalog_predicate_registry_freeze(&r->registry));
    r->rules[0].body[0].kind=255;
    forbid_allocator=1;
    maelys_datalog_policy_cache_reads(p);
    for(size_t i=0;i<r->registry.count;i++) CHECK(!answer(p,r->registry.defs[i].name,r->registry.defs[i].arity,1));
    CHECK(!answer(p,"builtin_probe",1,0)); CHECK(!answer(p,"unknown",2,0)); CHECK(!forbidden_calls);
    forbid_allocator=0;
    OK(maelys_datalog_policy_free(p)); puts("unknown literal reads all registry pairs PASS"); return 0;
}
static int failure_output_and_storage(void) {
    size_t bytes=0,alignment=0;
    OK(maelys_datalog_policy_storage_requirements(&bytes,&alignment));
    void *storage=malloc(bytes); CHECK(storage && (uintptr_t)storage%alignment==0);
    maelys_datalog_policy_t *p=NULL;
    const char *source="out(X):-seed(X).";
    OK(maelys_datalog_policy_load_frontend_in(storage,bytes,"reads","storage",source,strlen(source),NULL,&p,NULL));
    CHECK(!answer(p,"seed",1,1));
    int value=77;
    forbid_allocator=1;
    CHECK(maelys_datalog_policy_reads_predicate(NULL,0,"seed",1,&value)==MAELYS_DATALOG_STATUS_INVALID_ARGUMENT); CHECK(value==77);
    CHECK(maelys_datalog_policy_reads_predicate(p,0,NULL,1,&value)==MAELYS_DATALOG_STATUS_INVALID_ARGUMENT); CHECK(value==77);
    CHECK(maelys_datalog_policy_reads_predicate(p,0,"seed",1,NULL)==MAELYS_DATALOG_STATUS_INVALID_ARGUMENT);
    CHECK(maelys_datalog_policy_reads_predicate(p,1,"seed",1,&value)==MAELYS_DATALOG_STATUS_NOT_FOUND); CHECK(value==77);
    CHECK(!forbidden_calls); forbid_allocator=0;
    OK(maelys_datalog_policy_free(p));
    forbid_allocator=1;
    CHECK(maelys_datalog_policy_reads_predicate(p,0,"seed",1,&value)==MAELYS_DATALOG_STATUS_INVALID_STATE); CHECK(value==77);
    CHECK(!forbidden_calls); forbid_allocator=0; free(storage);
    printf("caller storage %zu alignment %zu; output/liveness PASS\n",bytes,alignment); return 0;
}
static int manifest_paths(void) {
    const char *sources[]={"out(X):-seed(X).","out(N):-sum(V,agg(V),N)."};
    const char *json="{\"policy_set_id\":\"reads\",\"policy_set_version\":\"1\",\"manifest_version\":\"1\",\"default_profile\":\"enforce\",\"strict_loading\":true,\"fail_closed\":true,\"created_for\":\"test\",\"capabilities\":[],\"policies\":[{\"policy_id\":\"0\",\"domain\":\"reads\",\"file\":\"0.dl\",\"sha256\":\"34b63df4313086ef1510f14e1312b07024338e19c60da4373303033138367635\",\"mode\":\"enforce\",\"enabled\":true,\"description\":\"reads\"},{\"policy_id\":\"1\",\"domain\":\"reads\",\"file\":\"1.dl\",\"sha256\":\"a39affb380b7f92d740137435db7858431d311015183680621c8860cb3e25132\",\"mode\":\"enforce\",\"enabled\":true,\"description\":\"reads\"}]}";
    const char *legacy="{\"policy_set_id\":\"reads\",\"policy_set_version\":\"1\",\"manifest_version\":\"1\",\"default_profile\":\"MAELYS-DATALOG-v2\",\"strict_loading\":true,\"fail_closed\":true,\"created_for\":\"test\",\"capabilities\":[],\"policies\":[{\"policy_id\":\"0\",\"domain\":\"reads\",\"file\":\"0.dl\",\"sha256\":\"34b63df4313086ef1510f14e1312b07024338e19c60da4373303033138367635\",\"mode\":\"enforce\",\"enabled\":true,\"description\":\"reads\"},{\"policy_id\":\"1\",\"domain\":\"reads\",\"file\":\"1.dl\",\"sha256\":\"a39affb380b7f92d740137435db7858431d311015183680621c8860cb3e25132\",\"mode\":\"enforce\",\"enabled\":true,\"description\":\"reads\"}]}";
    maelys_datalog_policy_bundle_entry_t bundle[]={{"0",sources[0],strlen(sources[0])},{"1",sources[1],strlen(sources[1])}};
    size_t bytes, alignment; OK(maelys_datalog_policy_storage_requirements(&bytes,&alignment));
    void *storage=malloc(bytes); CHECK(storage);
    char directory[]="/tmp/maelys-reads-manifest-XXXXXX"; CHECK(mkdtemp(directory));
    char path[256];
    for(size_t i=0;i<2;i++) { snprintf(path,sizeof(path),"%s/%zu.dl",directory,i); FILE *f=fopen(path,"w"); CHECK(f); CHECK(fputs(sources[i],f)>=0); CHECK(!fclose(f)); }
    snprintf(path,sizeof(path),"%s/manifest.json",directory); FILE *f=fopen(path,"w"); CHECK(f); CHECK(fputs(json,f)>=0); CHECK(!fclose(f));
    for(int mode=0;mode<3;mode++) {
        maelys_datalog_policy_t *p=NULL;
        if(mode==0) OK(maelys_datalog_policy_load_manifest_buffer(json,strlen(json),bundle,2,0,&p,NULL));
        else if(mode==1) OK(maelys_datalog_policy_load_manifest_text_in(storage,bytes,legacy,strlen(legacy),bundle,2,0,&p,NULL));
        else OK(maelys_datalog_policy_load_manifest(path,0,&p,NULL));
        forbid_allocator=1;
        for(size_t i=0;i<2;i++) {
            int reads=-1; OK(maelys_datalog_policy_reads_predicate(p,i,"seed",1,&reads)); CHECK(reads==(i==0));
            OK(maelys_datalog_policy_reads_predicate(p,i,"agg",1,&reads)); CHECK(reads==(i==1));
            maelys_datalog_program_t program={.ruleset=&p->set.policies[i]}; CHECK(!policy_reads_oracle(p,i,&program));
        }
        CHECK(!forbidden_calls); forbid_allocator=0; OK(maelys_datalog_policy_free(p));
    }
    CHECK(!remove(path));
    for(size_t i=0;i<2;i++) { snprintf(path,sizeof(path),"%s/%zu.dl",directory,i); CHECK(!remove(path)); }
    CHECK(!rmdir(directory)); free(storage); puts("file/buffer/caller-storage manifests preserve per-policy reads PASS"); return 0;
}

int main(void) {
    const char *atoms[]={"a"};
    maelys_datalog_domain_t domain={"reads",defs,6,atoms,1};
    OK(maelys_datalog_domain_register(&domain));
    CHECK(!named_cases()); CHECK(!unknown_is_all_registry_pairs()); CHECK(!failure_output_and_storage()); CHECK(!manifest_paths());
    return 0;
}
