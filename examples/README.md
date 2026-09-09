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
