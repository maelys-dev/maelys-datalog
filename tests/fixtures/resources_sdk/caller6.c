/* SPDX-License-Identifier: MPL-2.0 */
#include <maelys/datalog_resources.h>
#include <maelys/datalog_advanced.h>
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
const maelys_datalog_backend_t *matrix_provider5(void);
void matrix_provider5_counts(unsigned counts[4]);
const maelys_datalog_backend_v6_t *matrix_provider6(void);
unsigned matrix_provider6_prepared(void);
maelys_datalog_status_t matrix_resource_echo(const maelys_datalog_program_info_t *,
                                          const maelys_datalog_session_resources_t *,
                                          const maelys_datalog_session_resources_t *,
                                          maelys_datalog_session_resources_t *);

static void transport(void) {
    /* All exceed LARGE's present quotas; high also detects 32-bit narrowing on
     * a 64-bit public boundary. No fixture array is sized from these values. */
    const size_t high = SIZE_MAX > UINT32_MAX ? (size_t)UINT32_MAX + 19u : 524309u;
    struct {maelys_datalog_session_resources_t v; unsigned char tail[24];} in, out;
    memset(&in, 0xa7, sizeof(in)); memset(&out, 0x5b, sizeof(out));
    in.v = (maelys_datalog_session_resources_t)MAELYS_DATALOG_RESOURCES_INIT;
    out.v = (maelys_datalog_session_resources_t)MAELYS_DATALOG_RESOURCES_INIT;
    in.v.input_facts = 4097; in.v.derived_facts = 4099;
    in.v.symbols = 8193; in.v.text_bytes = high;
    maelys_datalog_session_resources_t ceilings = MAELYS_DATALOG_RESOURCES_INIT;
    ceilings.input_facts = high + 4; ceilings.derived_facts = high + 4;
    ceilings.symbols = high + 4; ceilings.text_bytes = high + 4;
    maelys_datalog_program_info_t bounds = {0};
    bounds.max_input_facts = ceilings.input_facts;
    bounds.max_derived_facts = ceilings.derived_facts; bounds.max_facts_per_predicate = 1025;
#define ECHO() matrix_resource_echo(&bounds, &ceilings, &in.v, &out.v)
#ifdef OPTIONAL_TAIL
    in.v.struct_size = sizeof(in); out.v.struct_size = sizeof(out);
#endif
    assert(ECHO() == 0);
    assert(out.v.input_facts == 4097 && out.v.derived_facts == 4099 &&
        out.v.symbols == 8193 && out.v.text_bytes == high);
    for (size_t i = 0; i < sizeof(out.tail); ++i) assert(out.tail[i] == 0x5b);
    in.v.input_facts = high; in.v.derived_facts = high + 1;
    in.v.symbols = high + 2; in.v.text_bytes = high + 3;
    assert(ECHO() == 0);
    assert(out.v.input_facts == high && out.v.derived_facts == high + 1 &&
        out.v.symbols == high + 2 && out.v.text_bytes == high + 3);
    in.v.text_bytes = high + 5;
    assert(ECHO() == MAELYS_DATALOG_STATUS_UNSUPPORTED);
    in.v.text_bytes = high;
    in.v.required_features = MAELYS_DATALOG_RESOURCE_CALLER_ALLOCATOR;
    assert(ECHO() == MAELYS_DATALOG_STATUS_UNSUPPORTED);
    in.v.required_features = UINT64_C(1) << 63;
    assert(ECHO() == MAELYS_DATALOG_STATUS_UNSUPPORTED);
    in.v.required_features = 0; in.v.memory_mode = MAELYS_DATALOG_MEMORY_BACKEND_ELASTIC;
    assert(ECHO() == MAELYS_DATALOG_STATUS_UNSUPPORTED);
    in.v.memory_mode = 0; in.v.contract_version = 2;
    assert(ECHO() == MAELYS_DATALOG_STATUS_UNSUPPORTED);
    in.v.contract_version = 1; in.v.struct_size = MAELYS_DATALOG_RESOURCES_PREFIX_SIZE;
    assert(ECHO() == MAELYS_DATALOG_STATUS_STORAGE_TOO_SMALL);
}
int main(void) {
    transport();
    const maelys_datalog_predicate_t predicates[] = {
        MAELYS_DATALOG_EDB("seed", 1), MAELYS_DATALOG_IDB_QUERY("seen", 1)};
    const maelys_datalog_domain_t domain = {"matrix", predicates, 2, NULL, 0};
    assert(maelys_datalog_domain_register(&domain) == 0);
    maelys_datalog_policy_t *p; const char *source = "seen(X) :- seed(X).";
    assert(maelys_datalog_policy_load_inline("matrix", "p", source, strlen(source), &p, NULL) == 0);
    maelys_datalog_session_config_t *c;
    assert(maelys_datalog_session_config_create(&c) == 0);
    struct {maelys_datalog_session_resource_request_t v; size_t tail[3];} request = {
        MAELYS_DATALOG_RESOURCE_REQUEST_INIT, {0, 0, 0}};
#ifdef OPTIONAL_TAIL
    request.v.struct_size = sizeof(request);
#endif
    request.v.capacity_mask = MAELYS_DATALOG_CAPACITY_INPUT_FACTS | MAELYS_DATALOG_CAPACITY_DERIVED_FACTS;
    request.v.input_facts = 4; request.v.derived_facts = 8;
    assert(maelys_datalog_session_config_set_resources(c, &request.v) == 0);
    assert(maelys_datalog_session_config_set_backend(c, matrix_provider5()) == 0);
    maelys_datalog_session_t *s = NULL;
    assert(maelys_datalog_session_create_configured(p, 0, c, &s) == MAELYS_DATALOG_STATUS_UNSUPPORTED && !s);
    unsigned counts[4]; matrix_provider5_counts(counts); assert(!counts[0]);
    assert(maelys_datalog_session_config_set_backend_v6(c, matrix_provider6()) == 0);
    assert(maelys_datalog_session_create_configured(p, 0, c, &s) == 0);
    assert(matrix_provider6_prepared() == 1);
    maelys_datalog_session_resources_t r = MAELYS_DATALOG_RESOURCES_INIT;
    assert(maelys_datalog_session_get_resources(s, &r) == 0 && r.input_facts == 4 && r.derived_facts == 8);
    const maelys_datalog_program_t *program; maelys_datalog_program_info_t bounds;
    assert(maelys_datalog_session_program(s, &program) == 0);
    assert(maelys_datalog_program_info(program, &bounds) == 0);
    assert(maelys_datalog_session_free(s) == 0);
    /* Actual loaded SMALL/LARGE host rejects an above-profile E before prepare. */
    request.v.input_facts = bounds.max_input_facts + 1;
    assert(maelys_datalog_session_config_set_resources(c, &request.v) == 0);
    assert(maelys_datalog_session_create_configured(p, 0, c, &s) == MAELYS_DATALOG_STATUS_UNSUPPORTED && !s);
    assert(matrix_provider6_prepared() == 1);
    assert(maelys_datalog_session_config_free(c) == 0);
    assert(maelys_datalog_policy_free(p) == 0);
    puts("ABI6 separate provider/tails; ABI5 non-default refusal; synthetic XLARGE transport and host refusal PASS");
    return 0;
}
