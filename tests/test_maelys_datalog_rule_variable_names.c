/* SPDX-License-Identifier: MPL-2.0 */
#if defined(__linux__) && !defined(_GNU_SOURCE)
#define _GNU_SOURCE 1
#endif
/* Installed public headers only; engine allocations are instrumented. */
#undef malloc
#undef calloc
#undef realloc
#undef free
#undef memset
#include <maelys/datalog_frontend.h>
#include <maelys/datalog_resources.h>
#include <stdint.h>
#if !MAELYS_DATALOG_HAS_RULE_VARIABLE_NAMES
#error "installed candidate SDK must expose the rule-variable-name capability"
#endif
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#define CHECK(x) do { if (!(x)) { fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #x); return 1; } } while (0)
#define OK(x) CHECK((x) == MAELYS_DATALOG_STATUS_OK)
static size_t attempts, live, fail_at;
static int disabled;
static int refuse(void) { ++attempts; return disabled || (fail_at && attempts == fail_at); }
void *maelys_test_malloc(size_t n) { if (refuse()) return NULL; void *p = malloc(n); if (p) ++live; return p; }
void *maelys_test_calloc(size_t n, size_t s) { if (refuse()) return NULL; void *p = calloc(n,s); if (p) ++live; return p; }
void *maelys_test_realloc(void *p, size_t n) { if (refuse()) return NULL; void *q = realloc(p,n); if (q && !p) ++live; return q; }
void maelys_test_free(void *p) { if (p) --live; free(p); }
void *maelys_test_memset(void *p, int c, size_t n) { return memset(p,c,n); }
static const maelys_datalog_predicate_t predicates[] = {
    {"quad",4,MAELYS_DATALOG_PREDICATE_EDB}, {"seed",1,MAELYS_DATALOG_PREDICATE_EDB}, {"edge",2,MAELYS_DATALOG_PREDICATE_EDB},
    {"pair",2,MAELYS_DATALOG_PREDICATE_IDB|MAELYS_DATALOG_PREDICATE_QUERY},
    {"visible",1,MAELYS_DATALOG_PREDICATE_IDB|MAELYS_DATALOG_PREDICATE_QUERY}
};
static int load(const char *source, maelys_datalog_policy_t **out) {
    maelys_datalog_diagnostic_t d = MAELYS_DATALOG_DIAGNOSTIC_INIT;
    int rc = maelys_datalog_policy_load_inline("rule_names","names",source,strlen(source),out,&d);
    if (rc && !fail_at) fprintf(stderr,"load: %s (%d)\n",d.message,rc);
    return rc;
}
static int name(const maelys_datalog_program_t *p, size_t rule, unsigned id, const char *expected) {
    const char *value = "sentinel";
    attempts = 0; disabled = 1;
    maelys_datalog_status_t rc = maelys_datalog_program_rule_variable_name(p,rule,id,&value);
    disabled = 0;
    CHECK(attempts == 0);
    if (expected) { CHECK(rc == MAELYS_DATALOG_STATUS_OK); CHECK(!strcmp(value,expected)); }
    else { CHECK(rc == MAELYS_DATALOG_STATUS_NOT_FOUND); CHECK(!strcmp(value,"sentinel")); }
    return 0;
}
static int mapping(void) {
    const char *sources[] = {
        "pair(User, Resource) :- edge(User, Resource).",
        "pair(Resource, User) :- edge(User, Resource).",
        "pair(U, Resource) :- edge(U, Resource).",
        "pair(User, 1) :- edge(User, _).",
        "pair(User, User) :- seed(User) or edge(User, Other).",
        "visible(Result) :- edge(Result, Input), Result > Input + 1.",
        "visible(User) :- seed(User), contains(User, \"x\").",
        "visible(Total) :- count(Item, seed(Item), Total)."
    };
    const char *expected[][3] = {{"User","Resource",NULL},{"Resource","User",NULL},
        {"Resource",NULL,"U"},{"User",NULL,NULL},{"User",NULL,NULL},
        {"Result","Input",NULL},{"User",NULL,NULL},{"Total","Item",NULL}};
    for (size_t i=0;i<sizeof(sources)/sizeof(sources[0]);++i) {
        char source[1024]; strcpy(source,sources[i]);
        maelys_datalog_policy_t *policy = NULL;
        CHECK(load(source,&policy) == 0);
        memset(source,'!',strlen(source)); /* No borrowing from caller source. */
        maelys_datalog_session_t *s;
        OK(maelys_datalog_session_create(policy,0,&s));
        const maelys_datalog_program_t *p;
        OK(maelys_datalog_session_program(s,&p));
        const char *first = NULL, *again = NULL;
        OK(maelys_datalog_program_rule_variable_name(p,0,0,&first));
        OK(maelys_datalog_program_rule_variable_name(p,0,0,&again)); CHECK(first == again);
        char before[65],after[65]; OK(maelys_datalog_program_fingerprint(p,before));
        OK(maelys_datalog_policy_free(policy));
        CHECK(name(p,0,0,expected[i][0]) == 0);
        CHECK(name(p,0,1,expected[i][1]) == 0);
        CHECK(name(p,0,20,expected[i][2]) == 0);
        CHECK(name(p,0,26,NULL) == 0); CHECK(name(p,0,31,NULL) == 0);
        CHECK(name(p,SIZE_MAX,0,NULL) == 0);
        if (i==4) { CHECK(name(p,1,0,"User") == 0); CHECK(name(p,1,1,"Other") == 0); }
        const char *out = "sentinel";
        CHECK(maelys_datalog_program_rule_variable_name(NULL,0,0,&out) == MAELYS_DATALOG_STATUS_INVALID_ARGUMENT);
        CHECK(maelys_datalog_program_rule_variable_name(p,0,32,&out) == MAELYS_DATALOG_STATUS_NOT_FOUND);
        CHECK(maelys_datalog_program_rule_variable_name(p,0,0,NULL) == MAELYS_DATALOG_STATUS_INVALID_ARGUMENT);
        CHECK(!strcmp(out,"sentinel"));
        OK(maelys_datalog_program_fingerprint(p,after)); CHECK(!strcmp(before,after));
        CHECK(!strcmp(first,expected[i][0]));
        OK(maelys_datalog_session_free(s));
    }
    /* Complete spelling at the lexer bound, not a truncated display label. */
    char longest[1025],source[2200]; memset(longest,'x',1024); longest[0]='L'; longest[1024]=0;
    snprintf(source,sizeof(source),"visible(%s) :- seed(%s).",longest,longest);
    maelys_datalog_policy_t *policy; CHECK(load(source,&policy) == 0);
    maelys_datalog_session_t *s; OK(maelys_datalog_session_create(policy,0,&s));
    const maelys_datalog_program_t *p; OK(maelys_datalog_session_program(s,&p));
    CHECK(name(p,0,0,longest) == 0);
    OK(maelys_datalog_session_free(s)); OK(maelys_datalog_policy_free(policy));
    return 0;
}
/* The custom frontend has typed variables but no claim of source spelling. */
static maelys_datalog_status_t synthetic(const char *source, size_t length,
    maelys_datalog_program_builder_t *builder, maelys_datalog_diagnostic_t *d) {
    (void)source; (void)length; (void)d;
    maelys_datalog_ir_rule_t r; memset(&r,0,sizeof(r));
    r.head.predicate="visible"; r.head.arity=1;
    r.head.terms[0].kind=MAELYS_DATALOG_IR_VARIABLE; r.head.terms[0].as.variable=20;
    r.body_count=1; r.body[0].kind=MAELYS_DATALOG_IR_ATOM;
    r.body[0].atom=r.head; r.body[0].atom.predicate="seed";
    return maelys_datalog_program_add_rule(builder,&r);
}
static int synthetic_names(void) {
    maelys_datalog_frontend_t f = {MAELYS_DATALOG_PROGRAM_ABI_VERSION,sizeof(f),"synthetic","names-v1",synthetic};
    maelys_datalog_policy_t *policy;
    OK(maelys_datalog_policy_load_frontend("rule_names","synthetic","x",1,&f,&policy,NULL));
    maelys_datalog_session_t *s; OK(maelys_datalog_session_create(policy,0,&s));
    const maelys_datalog_program_t *p; OK(maelys_datalog_session_program(s,&p));
    CHECK(name(p,0,20,NULL) == 0);
    OK(maelys_datalog_policy_free(policy)); OK(maelys_datalog_session_free(s)); return 0;
}
static const char manifest_source[] = "pair(User,Resource) :- edge(User,Resource).";
static const char manifest[] =
    "{\"policy_set_id\":\"names\",\"policy_set_version\":\"1\",\"manifest_version\":\"1\","
    "\"default_profile\":\"MAELYS-DATALOG-v2\",\"strict_loading\":true,\"fail_closed\":true,\"created_for\":\"test\","
    "\"capabilities\":[],\"policies\":[{\"policy_id\":\"names\",\"domain\":\"rule_names\",\"file\":\"names.dl\","
    "\"sha256\":\"d94c9f94fa6e0d7a03b29991a0e5beb031d9279ffc84d83129f7ed432b3dfd4b\",\"mode\":\"enforce\",\"enabled\":true,\"description\":\"names\","
    "\"idb_predicates\":[{\"name\":\"pair\",\"arity\":2}],\"queries\":[{\"name\":\"pair\",\"arity\":2}]}]}";
static void *aligned_storage(size_t bytes, size_t alignment, void **raw) {
    *raw=malloc(bytes+alignment); if (!*raw) return NULL;
    return (void *)(((uintptr_t)*raw+alignment-1)&~(uintptr_t)(alignment-1));
}
static int manifest_lifetime(void) {
    const maelys_datalog_policy_bundle_entry_t bundle={"names",manifest_source,sizeof(manifest_source)-1};
    for (int mode=0;mode<3;++mode) {
        size_t bytes,alignment; void *raw=NULL,*storage=NULL,*session_raw=NULL;
        maelys_datalog_policy_t *policy;
        if (mode) {
            OK(maelys_datalog_policy_storage_requirements(&bytes,&alignment));
            storage=aligned_storage(bytes,alignment,&raw); CHECK(storage);
            OK(maelys_datalog_policy_load_manifest_text_in(storage,bytes,manifest,sizeof(manifest)-1,&bundle,1,0,&policy,NULL));
        } else OK(maelys_datalog_policy_load_manifest_text(manifest,sizeof(manifest)-1,&bundle,1,0,&policy,NULL));
        maelys_datalog_session_t *s,*second;
        if (mode==2) {
            maelys_datalog_session_storage_plan_t plan=MAELYS_DATALOG_SESSION_PLAN_INIT;
            OK(maelys_datalog_session_storage_requirements_configured(policy,0,NULL,&plan,NULL));
            void *arena=aligned_storage(plan.arena_bytes,plan.arena_alignment,&session_raw); CHECK(arena);
            OK(maelys_datalog_session_init_configured(arena,plan.arena_bytes,policy,0,NULL,&s,NULL));
        } else OK(maelys_datalog_session_create(policy,0,&s));
        OK(maelys_datalog_session_create(policy,0,&second));
        const maelys_datalog_program_t *p; OK(maelys_datalog_session_program(s,&p));
        OK(maelys_datalog_policy_free(policy));
        if (storage) { memset(storage,0xa5,bytes); free(raw); }
        OK(maelys_datalog_session_free(second));
        CHECK(name(p,0,0,"User") == 0); CHECK(name(p,0,1,"Resource") == 0);
        OK(maelys_datalog_session_free(s)); free(session_raw);
    }
    return 0;
}
static int manifest_sources(void) {
    const maelys_datalog_policy_bundle_entry_t bundle={"names",manifest_source,sizeof(manifest_source)-1};
    char enforce[sizeof(manifest)];strcpy(enforce,manifest);
    char *profile=strstr(enforce,"MAELYS-DATALOG-v2");CHECK(profile);
    memmove(profile+7,profile+strlen("MAELYS-DATALOG-v2"),strlen(profile+strlen("MAELYS-DATALOG-v2"))+1);memcpy(profile,"enforce",7);
    maelys_datalog_policy_t *policy;OK(maelys_datalog_policy_load_manifest_buffer(enforce,strlen(enforce),&bundle,1,0,&policy,NULL));
    maelys_datalog_session_t *s;const maelys_datalog_program_t *p;
    OK(maelys_datalog_session_create(policy,0,&s));OK(maelys_datalog_session_program(s,&p));CHECK(name(p,0,0,"User")==0);
    OK(maelys_datalog_session_free(s));OK(maelys_datalog_policy_free(policy));
    char folder[]="/tmp/maelys-rule-names-XXXXXX";CHECK(mkdtemp(folder));
    char src[160],path[160];snprintf(src,sizeof(src),"%s/names.dl",folder);snprintf(path,sizeof(path),"%s/manifest.json",folder);
    FILE *file=fopen(src,"wb");CHECK(file);CHECK(fwrite(manifest_source,1,sizeof(manifest_source)-1,file)==sizeof(manifest_source)-1);CHECK(fclose(file)==0);
    file=fopen(path,"wb");CHECK(file);CHECK(fwrite(enforce,1,strlen(enforce),file)==strlen(enforce));CHECK(fclose(file)==0);
    OK(maelys_datalog_policy_load_manifest(path,0,&policy,NULL));OK(maelys_datalog_session_create(policy,0,&s));OK(maelys_datalog_session_program(s,&p));CHECK(name(p,0,1,"Resource")==0);
    OK(maelys_datalog_policy_free(policy));CHECK(name(p,0,0,"User")==0);OK(maelys_datalog_session_free(s));
    CHECK(unlink(src)==0);CHECK(unlink(path)==0);CHECK(rmdir(folder)==0);
    return 0;
}
static int rule_local_and_anonymous(void) {
    maelys_datalog_policy_t *policy;
    CHECK(load("pair(User,Resource) :- edge(User,Resource). pair(Member,Permission) :- edge(Member,Permission).", &policy)==0);
    maelys_datalog_session_t *s; OK(maelys_datalog_session_create(policy,0,&s));
    const maelys_datalog_program_t *p; OK(maelys_datalog_session_program(s,&p));
    CHECK(name(p,0,0,"User")==0); CHECK(name(p,0,1,"Resource")==0);
    CHECK(name(p,1,0,"Member")==0); CHECK(name(p,1,1,"Permission")==0);
    OK(maelys_datalog_session_free(s)); OK(maelys_datalog_policy_free(policy));
    CHECK(load("visible(1) :- seed(_).", &policy)==0);
    OK(maelys_datalog_session_create(policy,0,&s)); OK(maelys_datalog_session_program(s,&p));
    CHECK(name(p,0,0,NULL)==0); CHECK(name(p,0,26,NULL)==0);
    OK(maelys_datalog_policy_free(policy)); OK(maelys_datalog_session_free(s)); return 0;
}
static int limits_and_reuse(void) {
    /* Seven complete 1,024-byte spellings fit; the eighth does not. A short
     * ninth spelling must still fit, without a truncated or partially used name. */
    char *source = (char *)malloc(25000); CHECK(source);
    size_t used = 0;
    char longest[1025]; memset(longest, 'x', 1024); longest[1024] = 0;
    for (unsigned i = 0; i < 8; ++i) {
        longest[0] = (char)('A' + i);
        int n = snprintf(source + used, 25000 - used, "visible(%s) :- seed(%s).\n", longest, longest);
        CHECK(n > 0); used += (size_t)n;
    }
    strcpy(source + used, "visible(Short) :- seed(Short).");
    maelys_datalog_policy_t *policy; CHECK(load(source,&policy)==0);
    maelys_datalog_session_t *session; OK(maelys_datalog_session_create(policy,0,&session));
    const maelys_datalog_program_t *program; OK(maelys_datalog_session_program(session,&program));
    for (unsigned i=0;i<8;++i) { longest[0]=(char)('A'+i); CHECK(name(program,i,0,i<7?longest:NULL)==0); }
    CHECK(name(program,8,0,"Short")==0); CHECK(name(program,0,UINT32_MAX,NULL)==0);
    OK(maelys_datalog_policy_free(policy)); OK(maelys_datalog_session_free(session));
    /* A source clause may use every named slot, independently of pool limits. */
    strcpy(source,"pair(V0,V1) :- ");
    for (unsigned i=0;i<26;i+=4) {
        char atom[80]; snprintf(atom,sizeof(atom),"%squad(V%u,V%u,V%u,V%u)",i?", ":"",i,i+1,i+2<26?i+2:i,i+3<26?i+3:i);
        strcat(source,atom);
    }
    strcat(source,"."); CHECK(load(source,&policy)==0);
    OK(maelys_datalog_session_create(policy,0,&session)); OK(maelys_datalog_session_program(session,&program));
    for (unsigned i=0;i<26;++i) { char expected[8];snprintf(expected,sizeof(expected),"V%u",i);CHECK(name(program,0,i,expected)==0); }
    OK(maelys_datalog_session_free(session)); OK(maelys_datalog_policy_free(policy));
    source[0]=0;
    for (unsigned i=0;i<128;++i) { char rule[90];snprintf(rule,sizeof(rule),"visible(Var%u) :- seed(Var%u).\n",i,i);strcat(source,rule); }
    CHECK(load(source,&policy)==0); OK(maelys_datalog_session_create(policy,0,&session));
    OK(maelys_datalog_session_program(session,&program));
    for (unsigned i=0;i<128;++i) { char expected[16];snprintf(expected,sizeof(expected),"Var%u",i);CHECK(name(program,i,0,expected)==0); }
    OK(maelys_datalog_session_free(session)); OK(maelys_datalog_policy_free(policy));
    free(source);
    /* Failed loading must not leak names into an arena reused by another policy.
     * Frontend loading into caller-owned storage needs no engine allocations. */
    size_t bytes,alignment;OK(maelys_datalog_policy_storage_requirements(&bytes,&alignment));
    void *raw,*storage=aligned_storage(bytes,alignment,&raw);CHECK(storage);
    const char bad[]="visible(Stale) :- seed(Stale). visible(Broken) :- .";
    policy=NULL;
    CHECK(maelys_datalog_policy_load_frontend_in(storage,bytes,"rule_names","reuse",bad,sizeof(bad)-1,NULL,&policy,NULL)!=0);
    CHECK(!policy);
    const char fresh[]="visible(Z) :- seed(Z).";
    attempts=0;disabled=1;
    maelys_datalog_status_t rc=maelys_datalog_policy_load_frontend_in(storage,bytes,"rule_names","reuse",fresh,sizeof(fresh)-1,NULL,&policy,NULL);
    disabled=0;OK(rc);CHECK(attempts==0);
    OK(maelys_datalog_session_create(policy,0,&session));OK(maelys_datalog_session_program(session,&program));
    CHECK(name(program,0,0,NULL)==0);CHECK(name(program,0,25,"Z")==0);CHECK(name(program,1,0,NULL)==0);
    OK(maelys_datalog_policy_free(policy));memset(storage,0xa5,bytes);free(raw);
    CHECK(name(program,0,25,"Z")==0);OK(maelys_datalog_session_free(session));
    /* Convenience creation can recycle its block; names from the old snapshot
     * must not survive a new clause using the same numeric slot. */
    CHECK(load("visible(New) :- seed(New).",&policy)==0);
    OK(maelys_datalog_session_create(policy,0,&session));OK(maelys_datalog_session_program(session,&program));
    CHECK(name(program,0,0,"New")==0);CHECK(name(program,0,25,NULL)==0);
    OK(maelys_datalog_session_free(session));OK(maelys_datalog_policy_free(policy));
    return 0;
}
#ifdef RULE_NAMES_ALLOCATION_TEST
static int allocation_failure(void) {
    /* Warm registry/context state, then reject every allocation in turn. */
    maelys_datalog_policy_t *p; CHECK(load(manifest_source,&p)==0); OK(maelys_datalog_policy_free(p));
    size_t before=live, rejected=0;
    for (size_t n=1;n<128;++n) {
        attempts=0; fail_at=n; p=NULL; int rc=load(manifest_source,&p); fail_at=0;
        if (!rc) { OK(maelys_datalog_policy_free(p)); CHECK(live==before); break; }
        CHECK(!p); CHECK(live==before); ++rejected;
    }
    CHECK(rejected==1); /* Policy object only: display metadata adds no allocation. */
    printf("rule names: %zu inline allocation failures leave no live allocation\n",rejected);
    const maelys_datalog_policy_bundle_entry_t bundle={"names",manifest_source,sizeof(manifest_source)-1};
    rejected=0;
    for (size_t n=1;n<128;++n) {
        attempts=0; fail_at=n; p=NULL;
        int rc=maelys_datalog_policy_load_manifest_text(manifest,sizeof(manifest)-1,&bundle,1,0,&p,NULL);
        fail_at=0;
        if (!rc) { OK(maelys_datalog_policy_free(p)); CHECK(live==before); break; }
        CHECK(!p); CHECK(live==before); ++rejected;
    }
    CHECK(rejected==4);
    printf("rule names: %zu manifest allocation failures leave no live allocation\n",rejected);
    /* Reject a later entry after the first rule's name block was retained. */
    char bad[2048]; int n=snprintf(bad,sizeof(bad),"%.*s,{}]}",(int)sizeof(manifest)-3,manifest);
    CHECK(n>0 && (size_t)n<sizeof(bad)); p=NULL;
    CHECK(maelys_datalog_policy_load_manifest_text(bad,(size_t)n,&bundle,1,0,&p,NULL)!=0);
    CHECK(!p && live==before);
    return 0;
}
#endif
int main(void) {
    const maelys_datalog_domain_t domain={"rule_names",predicates,5,NULL,0};
    OK(maelys_datalog_domain_register(&domain));
    CHECK(mapping()==0); CHECK(synthetic_names()==0); CHECK(manifest_lifetime()==0); CHECK(manifest_sources()==0);
    CHECK(rule_local_and_anonymous()==0); CHECK(limits_and_reuse()==0);
#ifdef RULE_NAMES_ALLOCATION_TEST
    CHECK(allocation_failure()==0);
#endif
    puts("rule variable names: mapping, refusals and lifetime PASS"); return 0;
}
