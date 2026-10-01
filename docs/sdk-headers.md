# Installed SDK headers

The SDK has eleven headers, grouped by the reader's role. This is an include
migration: function names, constants, public record layouts and backend ABI
5/6/7 callback contracts are unchanged. It adds no allocator service and changes
no runtime behavior. Rebuild source consumers after updating their includes;
existing binaries keep their ABI.

| Audience | Header under `maelys/` | Declarations |
| --- | --- | --- |
| Application | `datalog.h` | Opaque handles, values, diagnostics, semantic enums, manifests, queries, text explanations, statistics and decisions |
| Application | `datalog_builders.h` | Declaration initializers and query builders |
| Application | `datalog_window.h` | Both window adapters |
| Application | `datalog_inputs.h` | Retained session inputs and explicit replacement/deltas |
| Application | `datalog_resources.h` | Session capacities, storage plans and caller-owned policy/backend storage records |
| Application | `datalog_program.h` | Read-only compiled-program inspection and IR records |
| Application | `datalog_explanations.h` | Structured views of prepared explanations |
| Integrator | `datalog_extension.h` | Contexts, extension registration, backend/frontend selection and incomplete descriptor names |
| Component author | `datalog_module.h` | Filter/planner descriptors and domain builder callbacks |
| Component author | `datalog_frontend.h` | Frontend descriptor and program construction |
| Component author | `datalog_backend.h` | Complete backend descriptors for ABI 5, 6 and 7 and host services for providers |

An application header never includes an integrator or component-author header.
The extension registry points to incomplete descriptors: their definitions live
only in their component-author headers. Code that inspects a descriptor, rather
than selecting or registering it by pointer, must include its defining header.
The same-level guarded pair `datalog.h` / `datalog_builders.h` is intentional.
The caller-owned `backend_storage_t` buffer record in `datalog_resources.h` is
not a provider descriptor and exposes no backend callback.

## Source migration

The four old paths below are removed, with no forwarding headers or fallback.

| Former include | Replacement |
| --- | --- |
| `datalog_details.h` | `datalog.h` |
| `datalog_transactions.h` | `datalog_inputs.h` |
| `datalog_backend_transactions.h` | `datalog_backend.h` |
| `datalog_advanced.h` | The role-specific headers above: facade, resources, explanations, extension, module or frontend as used |

Additionally, code formerly obtaining a descriptor transitively through
`datalog_extension.h`, `datalog_resources.h` or `datalog_program.h` must include
`datalog_backend.h`, `datalog_frontend.h` or `datalog_module.h` explicitly.
Program mutation is in `datalog_frontend.h`; program inspection remains in
`datalog_program.h`. Function names do not change.

`policy_load_manifest_text` and its caller-owned variant remain supported.
They validate the historical `MAELYS-DATALOG-v2` manifest profile, whereas
`policy_load_manifest_buffer` validates `enforce`. They are not interchangeable;
this header-only migration does not remove either behavior.

Six descriptor structs acquire explicit tags so they can be forward-declared.
Each tag uses its existing typedef name, preserving C++ type linkage as well
as the C layouts. Field types, order, size, alignment and callback signatures
are preserved. Neither the consumer API version nor backend ABI negotiation
changes. Previously published SDK archives remain immutable.

## Verification contract

`tools/check_module_sdk.sh` compiles each installed header in C11 and C++17,
checks negative consumers of removed/private headers, and runs copied consumers,
examples and starters outside the checkout against static and shared libraries.
It invokes `tools/check_sdk_headers.py` to check the audience include graph,
application/provider visibility, incomplete integrator descriptors and all six
orders of the component-author includes.

`tools/check_header_relocation.py BEFORE_PREFIX AFTER_PREFIX` compares the
normalized multiset of public declarations and non-guard macros, checks builders
byte-for-byte, and uses Clang ASTs to enumerate every complete public record and
compare native `sizeof`, alignment and field offsets, plus separately compiled
C++ descriptor type names. Its only normalization of
types permits the six new tags and their forward declarations. Run it against
clean installed prefixes; generated layouts and measurements are not committed.
This is a native-target layout check, not a claim about all possible C ABIs.

The migration also requires separately compiled old/new callers and providers
against both host libraries, an export and native code-section comparison with
the same compiler/profile, and a clean installed-SDK replay of known downstream
consumers. Inspect the full replay: replacing only the names of removed headers
can miss an author header previously included transitively. No downstream
implementation is copied into this repository.
