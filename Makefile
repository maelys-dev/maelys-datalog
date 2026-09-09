CC = clang
CFLAGS = -Wall -Wextra -g -I. -Iinclude
LIBS =
BUILD_DIR ?= build
OBJ_DIR = $(BUILD_DIR)/obj

ENGINE_SRCS = $(shell sed -n '/^[^\#]/p' build-support/core-sources.txt build-support/standard-sources.txt build-support/native-sources.txt)

COMMON_SRCS =

SRCS = $(ENGINE_SRCS) $(COMMON_SRCS)
OBJS = $(patsubst %.c,$(OBJ_DIR)/%.o,$(SRCS))

DOMAIN_FIXTURE_SRCS  = tests/fixtures/domains/maelys_datalog_example_domains.c
EXAMPLES_CHECK = examples/maelys_datalog_examples_check.c
DOMAIN_FIXTURE_OBJS  = $(patsubst %.c,$(OBJ_DIR)/%.o,$(DOMAIN_FIXTURE_SRCS))
EXAMPLES_BIN   = $(BUILD_DIR)/examples/maelys_datalog_examples_check

TEST_HELPER_SRCS = \
	tests/helpers/test_log.c \
	tests/helpers/test_framework.c

WASM_TEST_SRCS = \
	bindings/wasm/maelys_datalog_wasm.c
WASM_TEST_HEADERS = bindings/wasm/maelys_datalog_wasm.h

TEST_SRCS = $(wildcard tests/test_*.c)
TEST_BINS = $(TEST_SRCS:tests/%.c=$(BUILD_DIR)/tests/%)
TEST_CFLAGS = $(CFLAGS) -DMAELYS_TESTING
ENGINE_HEADERS = $(wildcard include/maelys/*.h src/core/*.h src/compiler/*.h src/public/*.h src/registry/*.h modules/standard/*.h)

$(OBJ_DIR)/%.o: %.c $(ENGINE_HEADERS)
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c $< -o $@

libmaelys_datalog.a: $(OBJS)
	ar rcs $@ $^

.PHONY: examples
examples: libmaelys_datalog.a $(DOMAIN_FIXTURE_OBJS) $(EXAMPLES_CHECK)
	@mkdir -p $(dir $(EXAMPLES_BIN))
	$(CC) $(CFLAGS) $(DOMAIN_FIXTURE_OBJS) $(EXAMPLES_CHECK) -L. -lmaelys_datalog $(LIBS) -o $(EXAMPLES_BIN)
	$(EXAMPLES_BIN)

$(BUILD_DIR)/tests/test_maelys_datalog_modules: TEST_EXTRA_SRCS = sdk/examples/filter/src/extension.c
$(BUILD_DIR)/tests/test_maelys_datalog_modules: TEST_CFLAGS += -pthread
$(BUILD_DIR)/tests/test_maelys_datalog_modules: sdk/examples/filter/src/extension.c
$(BUILD_DIR)/tests/test_maelys_datalog_compiler: TEST_EXTRA_SRCS = sdk/examples/frontend/src/extension.c sdk/examples/backend/src/extension.c
$(BUILD_DIR)/tests/test_maelys_datalog_compiler: sdk/examples/frontend/src/extension.c sdk/examples/backend/src/extension.c
$(BUILD_DIR)/tests/test_maelys_datalog_context: TEST_EXTRA_SRCS = sdk/examples/frontend/src/extension.c sdk/examples/backend/src/extension.c
$(BUILD_DIR)/tests/test_maelys_datalog_context: TEST_CFLAGS += -pthread
$(BUILD_DIR)/tests/test_maelys_datalog_context: sdk/examples/frontend/src/extension.c sdk/examples/backend/src/extension.c
$(BUILD_DIR)/tests/test_maelys_datalog_pipeline: TEST_EXTRA_SRCS = sdk/examples/frontend/src/extension.c
$(BUILD_DIR)/tests/test_maelys_datalog_pipeline: sdk/examples/frontend/src/extension.c

.PHONY: bench-pipeline
bench-pipeline: $(BUILD_DIR)/tests/test_maelys_datalog_pipeline
	./$(BUILD_DIR)/tests/test_maelys_datalog_pipeline --bench

$(BUILD_DIR)/tests/%: tests/%.c $(SRCS) $(DOMAIN_FIXTURE_SRCS) $(TEST_HELPER_SRCS) $(WASM_TEST_SRCS) $(WASM_TEST_HEADERS) $(ENGINE_HEADERS) | $(BUILD_DIR)/tests
	$(CC) $(TEST_CFLAGS) -I. -Iinclude $(SRCS) $(DOMAIN_FIXTURE_SRCS) $(TEST_HELPER_SRCS) $(WASM_TEST_SRCS) $(TEST_EXTRA_SRCS) $< -o $@

$(BUILD_DIR)/tests:
	mkdir -p $@

test: $(TEST_BINS)
	@set -e; for b in $(TEST_BINS); do echo "--- $$b ---"; ./$$b; done

.PHONY: check-version-header
check-version-header:
	@tmp="$(BUILD_DIR)/maelys_datalog_version.h.tmp"; \
	target="include/maelys_datalog_version.h"; \
	mkdir -p "$(BUILD_DIR)"; \
	bash scripts/generate-version-header.sh "$$tmp" >/dev/null; \
	if ! cmp -s "$$tmp" "$$target"; then \
		echo "error: $$target is out of sync with VERSION ($$(cat VERSION))." >&2; \
		echo "Run: scripts/generate-version-header.sh" >&2; \
		rm -f "$$tmp"; \
		exit 1; \
	fi; \
	rm -f "$$tmp"; \
	echo "$$target matches VERSION ($$(cat VERSION))"

.PHONY: check
check: test check-version-header

.PHONY: test_maelys_datalog_boundary
test_maelys_datalog_boundary: $(BUILD_DIR)/tests/test_maelys_datalog_boundary
	./$(BUILD_DIR)/tests/test_maelys_datalog_boundary

.PHONY: test_maelys_datalog_atom_vocabulary
test_maelys_datalog_atom_vocabulary: $(BUILD_DIR)/tests/test_maelys_datalog_atom_vocabulary
	./$(BUILD_DIR)/tests/test_maelys_datalog_atom_vocabulary

.PHONY: test_maelys_datalog_determinism
test_maelys_datalog_determinism: $(BUILD_DIR)/tests/test_maelys_datalog_determinism
	./$(BUILD_DIR)/tests/test_maelys_datalog_determinism

.PHONY: test_maelys_datalog_corpus
test_maelys_datalog_corpus: $(BUILD_DIR)/tests/test_maelys_datalog_corpus
	./$(BUILD_DIR)/tests/test_maelys_datalog_corpus

clean:
	rm -rf $(BUILD_DIR)
	rm -f libmaelys_datalog.a
