<!-- SPDX-License-Identifier: MIT -->
# Filter starter

This is an intentionally unimplemented extension, not a working algorithm.
It registers through the public SDK; execution callbacks return `UNSUPPORTED`.
The smoke test verifies that contract, not production conformance.

## Build after copying this directory

```sh
cmake -S . -B build -DMAELYS_SDK_PREFIX=/absolute/path/to/installed/sdk
cmake --build build
ctest --test-dir build --output-on-failure
```

Use another build directory with `-DMAELYS_SDK_SHARED=ON` to test shared-engine
linkage. Build output belongs in `build/`, never in `src/`.
Only installed SDK headers and libraries are needed; no engine source checkout
or MPL conformance helper is copied into this project.

## Implement your extension

Implement pattern validation, positive bounded costs and boolean evaluation.
Test invalid patterns, empty/binary inputs, overflow and callback errors.
Keep callbacks deterministic, bounded, allocation-free and reentrant.

Rename the public entrypoint, component/package names and semantic IDs.
Replace the rejection smoke checks with your feature tests as you implement
callbacks. Never turn an unimplemented operation into a successful empty result.
Read the matching SDK callback contracts for lifetimes and ownership.
The separate MPL conformance kit can be used for testing under its own license.

## License

All files in this starter directory are MIT; see `LICENSE`. You may copy and
adapt them for an open-source or proprietary extension, retaining the MIT
copyright and permission notice. This does not relicense the separately supplied
MPL SDK/engine or any MPL example code you might later copy. Include the required
MPL notices and source-access information when distributing the engine.
