# Contributing to Maelys Datalog

Thank you for helping improve Maelys Datalog.

## Before opening a pull request

1. Open an issue for substantial API or semantic changes.
2. Keep changes focused and preserve deterministic, fail-closed behavior.
3. Add tests for every behavior change.
4. Run the native test suite:

   ```sh
   make test
   ```

5. Run the sanitizer suite when changing parsing, memory ownership, or solver
   internals:

   ```sh
   make -f Makefile.asan asan
   ```

## Code expectations

- Target portable C11.
- Compile cleanly with `-Wall -Wextra`.
- Avoid hidden heap allocation on bounded evaluation paths.
- Document public API and ownership rules.
- Keep commits small enough to review.

By submitting a contribution, you agree that it may be distributed under the
project's MIT License.
