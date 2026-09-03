#include "src/manifest/maelys_datalog_manifest.h"

#include "common/maelys_sha256.h"

#include <stdint.h>
#include <string.h>

static void hash_u64(maelys_sha256_ctx_t *hash, uint64_t value) {
    unsigned char bytes[8];
    for (size_t i = 0u; i < sizeof(bytes); ++i)
        bytes[sizeof(bytes) - 1u - i] = (unsigned char)(value >> (8u * i));
    maelys_sha256_update(hash, bytes, sizeof(bytes));
}

static void hash_string(maelys_sha256_ctx_t *hash, const char *value) {
    size_t length = strlen(value);
    hash_u64(hash, (uint64_t)length);
    maelys_sha256_update(hash, (const unsigned char *)value, length);
}

maelys_result_t maelys_datalog_policy_set_fingerprint(
    const maelys_datalog_policy_set_t *set,
    char out_hex[65]) {
    if (!set || !out_hex) return MAELYS_ERR_INVALID_ARGUMENT;
    out_hex[0] = '\0';
    if (set->policy_count == 0u ||
        set->policy_count > sizeof(set->policies) / sizeof(set->policies[0]) ||
        set->query_whitelist_count > MAELYS_DATALOG_MAX_QUERY_WHITELIST)
        return MAELYS_ERR_INVALID_STATE;
    maelys_sha256_ctx_t hash;
    maelys_sha256_init(&hash);
    static const unsigned char domain[] = "MAELYS-DATALOG-POLICY-SET-v1";
    maelys_sha256_update(&hash, domain, sizeof(domain));
    hash_u64(&hash, (uint64_t)set->policy_count);
    for (size_t i = 0u; i < set->policy_count; ++i) {
        const maelys_datalog_ruleset_t *policy = &set->policies[i];
        if (!policy->loaded || !policy->policy_id[0] || !policy->domain[0] ||
            !maelys_sha256_hex_is_lowercase(policy->sha256))
            return MAELYS_ERR_INVALID_STATE;
        hash_string(&hash, policy->policy_id);
        hash_string(&hash, policy->domain);
        hash_string(&hash, policy->sha256);
    }
    hash_u64(&hash, (uint64_t)(set->enforces_query_whitelist != 0));
    hash_u64(&hash, (uint64_t)set->query_whitelist_count);
    for (size_t i = 0u; i < set->query_whitelist_count; ++i) {
        if (!set->query_whitelist[i].name[0]) return MAELYS_ERR_INVALID_STATE;
        hash_string(&hash, set->query_whitelist[i].name);
        hash_u64(&hash, (uint64_t)set->query_whitelist[i].arity);
    }
    unsigned char digest[MAELYS_SHA256_DIGEST_BYTES];
    maelys_sha256_final(&hash, digest);
    static const char hex[] = "0123456789abcdef";
    for (size_t i = 0u; i < sizeof(digest); ++i) {
        out_hex[2u * i] = hex[digest[i] >> 4u];
        out_hex[2u * i + 1u] = hex[digest[i] & 0x0fu];
    }
    out_hex[64] = '\0';
    return MAELYS_OK;
}
