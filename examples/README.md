# Using the engine

This directory contains application examples using Maelys Datalog. For examples
that implement extensions, see [the extension SDK](../sdk/README.md).

From the repository root:

```sh
make examples
```

The command builds and runs `build/examples/maelys_datalog_examples_check`.
Its macOS `.dSYM` bundle is generated there too. `BUILD_DIR` may select another
output directory; no executable or debug bundle belongs in this source directory.

The [temporal quota consumer example](temporal_quota.c) checks the difference
between a proposed action, a decision and a performed event, and prints complete
queried storage plans. See the [v0.21.0 guide](../docs/guides/temporal-quotas.md)
for installed-SDK commands and the FIFO counterexample. CMake's
`temporal_quota_consumer_contract` runs it with assertions enabled.
