<!-- SPDX-License-Identifier: CC-BY-4.0 -->
# maelys-datalog-domain-v1

Status: normative for the `maelys-datalog` command. This document defines the
JSON declaration used to register a domain through the installed consumer API.
It does not change the engine's policy language or manifest format.

A domain is one UTF-8 JSON object:

```json
{
  "format": "maelys-datalog-domain-v1",
  "name": "rbac",
  "predicates": [
    {"name": "role_permission", "arity": 2, "role": "policy_fact"},
    {"name": "request", "arity": 2, "role": "edb"},
    {"name": "allow", "arity": 2, "role": "idb", "query": true}
  ],
  "atoms": ["admin", "billing:read"]
}
```

All four top-level members are required; unknown members are rejected.
`format` is the exact string shown above. `name` is a nonempty string of at
most 63 UTF-8 bytes. `predicates` and `atoms` are arrays, including empty
arrays. Every predicate has exactly `name`, `arity`, `role`, and optionally
`query`. Its name is nonempty and at most 63 UTF-8 bytes. `arity` is a JSON
integer from zero through the loaded engine's `MAX_ARITY` (currently four).
`role` is exactly `edb`, `idb`, or `policy_fact`; `query` is a JSON boolean
defaulting to false. The roles map to the corresponding public predicate
flags, and true adds `MAELYS_DATALOG_PREDICATE_QUERY`. The engine performs
the final declaration validation at registration. Duplicate predicate names
are rejected, with the zero-based index of the second occurrence.

Every atom is a nonempty string. The maximum number of atoms and the maximum
UTF-8 byte length of each atom come from `maelys_datalog_limit_get` with
`MAELYS_DATALOG_LIMIT_MAX_POLICY_ATOMS` and
`MAELYS_DATALOG_LIMIT_MAX_POLICY_ATOM_BYTES`. Duplicate atoms are rejected,
with the second index. Predicate count is bounded by the loaded library's
`MAX_PREDICATES`. These are library build limits, not session quotas.

Parsing uses `maelys-json`'s RFC8259 profile: duplicate decoded object keys,
invalid UTF-8, U+0000, a BOM, trailing bytes, and malformed escapes or
surrogate pairs are rejected. A file that cannot be read or parsed is a CLI
input failure (exit 1). Under `check`, a well-formed JSON document that
violates this domain contract or is refused by domain registration is a
validation report (exit 2, `ok: true`, `data.valid: false`). Under `solve`,
`fingerprint`, and `explain`, it is an input failure (exit 1).

For a manifest, `check` accepts repeated `--domain` options and registers
every named domain before loading the policy set. Domain names must be
distinct. An inline policy accepts exactly one domain. Policy atoms authorize
constants in policy source; runtime EDB strings need no atom declaration.

The command is an allocating application. Its parser, input buffers, report
writer, and framework are outside the engine's allocation guarantees.
