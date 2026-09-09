# Extension developer kit

This directory groups resources for authors of frontend, backend, planner and
filter extensions. The public C contracts remain in `include/maelys/`; production
providers shipped with the engine remain in `modules/standard/`.

- [Examples](examples/README.md): four focused projects plus a frontend/filter
  bundle, all with the same layout, build commands and testing conventions.
- [Conformance](conformance/README.md): test-only helpers installed with the SDK.
- [Contexts and migration](../docs/architecture/extension-contexts.md): declare,
  register, seal and select extensions while preserving legacy integrations.

The root `examples/` directory is for applications using the engine, not for
extension implementations. Keep build output under `build/`, outside source
directories; follow the out-of-source commands in the example guide.
