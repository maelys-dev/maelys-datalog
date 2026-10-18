/* SPDX-License-Identifier: MPL-2.0 */
#include <maelys/datalog.h>
#include <stdio.h>
#include <string.h>
#define CHECK(x) do { if (!(x)) { fprintf(stderr, "line %d: %s\n", __LINE__, #x); return 1; } } while (0)
#define OK(x) CHECK((x) == MAELYS_DATALOG_STATUS_OK)
static int check(const char *source, const maelys_datalog_fact_t *facts, size_t count,
                 const char *predicate, int64_t a, int64_t b) {
    maelys_datalog_policy_t *p = NULL;
    maelys_datalog_session_t *s = NULL;
    maelys_datalog_result_t *r = NULL;
    maelys_datalog_diagnostic_t d = MAELYS_DATALOG_DIAGNOSTIC_INIT;
    int rc = maelys_datalog_policy_load_inline("names", "names", source, strlen(source), &p, &d);
    if (rc) fprintf(stderr,"%s\n",d.message);
    OK(rc);
    OK(maelys_datalog_session_create(p, 0, &s));
    OK(maelys_datalog_session_solve(s, facts, count, &r, &d));
    maelys_datalog_value_t q[] = {{.kind=MAELYS_DATALOG_VALUE_INTEGER,.as.integer=a},
                                 {.kind=MAELYS_DATALOG_VALUE_INTEGER,.as.integer=b}};
    int found = 0;
    OK(maelys_datalog_result_query(r,predicate,q,2,&found)); CHECK(found);
    OK(maelys_datalog_result_free(r)); OK(maelys_datalog_session_free(s));
    OK(maelys_datalog_policy_free(p)); return 0;
}
#define INT(n) {.kind=MAELYS_DATALOG_VALUE_INTEGER,.as.integer=(n)}
int main(void) {
    maelys_datalog_predicate_t defs[] = {
        {"edge",2,MAELYS_DATALOG_PREDICATE_EDB},
        {"seed",2,MAELYS_DATALOG_PREDICATE_EDB},
        {"quad",4,MAELYS_DATALOG_PREDICATE_EDB},
        {"out",2,MAELYS_DATALOG_PREDICATE_IDB|MAELYS_DATALOG_PREDICATE_QUERY}};
    const char *atoms[] = {"a"};
    maelys_datalog_domain_t domain = {"names",defs,4,atoms,1};
    OK(maelys_datalog_domain_register(&domain));
    maelys_datalog_fact_t facts[] = {
        {.predicate="seed",.arity=2,.terms={INT(7),INT(1)}},
        {.predicate="edge",.arity=2,.terms={INT(1),INT(2)}},
        {.predicate="edge",.arity=2,.terms={INT(2),INT(3)}}};
    CHECK(!check("out(U,Ux) :- edge(U,Ux), Ux > U.",facts,3,"out",1,2));
    CHECK(!check("out(U,G2) :- out(U,G1), edge(G1,G2). out(U,G) :- seed(U,G).",facts,3,"out",7,3));
    CHECK(!check("out(Ux,A) :- edge(Ux,A), A > Ux.",facts,3,"out",1,2));
    CHECK(!check("out(Id,Service) :- edge(Id,Service), Service = Id + 1.",facts,3,"out",1,2));
    CHECK(!check("out(U,Ux) :- edge(U,Ux), edge(Ux,_), edge(_,Ux).",facts,3,"out",1,2));
    /* Reset names per clause, and reserve letters in a later OR alternative. */
    CHECK(!check("out(Ux,A) :- edge(Ux,A) or seed(Ux,A). out(A,Z) :- seed(A,Z).",facts,3,"out",1,2));
    maelys_datalog_policy_t *invalid=NULL;
    const char *filter="out(U,U) :- seed(U,U), starts_with(Ux, \"a\").";
    CHECK(maelys_datalog_policy_load_inline("names","filter",filter,strlen(filter),&invalid,NULL)
          != MAELYS_DATALOG_STATUS_OK);
    CHECK(!invalid);
    char source[2048];
    for (unsigned count=26; count<=27; ++count) {
        strcpy(source,"out(V0,V1) :- ");
        for (unsigned i=0;i<count;i+=4) {
            char atom[64]; snprintf(atom,sizeof(atom),"%squad(V%u,V%u,V%u,V%u)",i?", ":"",i,(i+1<count)?i+1:i,(i+2<count)?i+2:i,(i+3<count)?i+3:i);
            strcat(source,atom);
        }
        strcat(source,".");
        maelys_datalog_policy_t *p=NULL;
        maelys_datalog_diagnostic_t d=MAELYS_DATALOG_DIAGNOSTIC_INIT;
        int rc=maelys_datalog_policy_load_inline("names","capacity",source,strlen(source),&p,&d);
        if(count==26) { if(rc) fprintf(stderr,"capacity: %s %zu %zu\n",d.message,d.observed_count,d.limit); OK(rc); OK(maelys_datalog_policy_free(p)); }
        else { CHECK(rc==MAELYS_DATALOG_STATUS_PAYLOAD_TOO_LARGE); CHECK(!p);
            CHECK(d.present & MAELYS_DATALOG_DIAGNOSTIC_CAPACITY);
            CHECK(d.limit_kind==MAELYS_DATALOG_LIMIT_MAX_RULE_VARIABLES);
            CHECK(!strcmp(d.token,"V26") && d.line==1 && d.column>0);
            CHECK(d.observed_count==27 && d.limit==26); }
    }
    strcpy(source,"out(V0,V1) :- ");
    for (unsigned i=0;i<24;i+=4) {
        char atom[64]; snprintf(atom,sizeof(atom),"%squad(V%u,V%u,V%u,V%u)",i?", ":"",i,i+1,i+2,i+3);
        strcat(source,atom);
    }
    strcat(source,", quad(V24,V25,_,_), quad(_,_,_,_).");
    maelys_datalog_policy_t *full=NULL;
    OK(maelys_datalog_policy_load_inline("names","full",source,strlen(source),&full,NULL));
    OK(maelys_datalog_policy_free(full));
    /* A fresh clause has its own 26-name budget, even after a full clause. */
    char second[2048]; strcpy(second,source);
    for (char *c=second;*c;++c) if (*c=='V') *c='W';
    strcat(source,"\n");
    strcat(source,second);
    maelys_datalog_diagnostic_t scope_diag=MAELYS_DATALOG_DIAGNOSTIC_INIT;
    int scope_rc=maelys_datalog_policy_load_inline("names","scopes",source,strlen(source),&full,&scope_diag);
    if(scope_rc) fprintf(stderr,"scope: %s %s\n",scope_diag.message,source);
    OK(scope_rc);
    OK(maelys_datalog_policy_free(full));
    puts("variable names: PASS"); return 0;
}
