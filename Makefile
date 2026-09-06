CC = clang
CFLAGS = -Wall -Wextra -g -I. -Iinclude
LIBS =
BUILD_DIR ?= build
OBJ_DIR = $(BUILD_DIR)/obj

ENGINE_SRCS = $(shell sed -n '/^[^\#]/p' build-support/core-sources.txt build-support/standard-sources.txt build-support/native-sources.txt)

COMMON_SRCS =

SRCS = $(ENGINE_SRCS) $(COMMON_SRCS)
OBJS = $(patsubst %.c,$(OBJ_DIR)/%.o,$(SRCS))

EXAMPLES_SRCS  = examples/domains/maelys_datalog_example_domains.c
EXAMPLES_CHECK = examples/maelys_datalog_examples_check.c
EXAMPLES_OBJS  = $(patsubst %.c,$(OBJ_DIR)/%.o,$(EXAMPLES_SRCS))
EXAMPLES_BIN   = examples/maelys_datalog_examples_check

TEST_HELPER_SRCS = \
	tests/helpers/test_log.c \
	tests/helpers/test_framework.c

WASM_TEST_SRCS = \
	src/wasm/maelys_datalog_wasm.c

TEST_SRCS = $(wildcard tests/test_*.c)
TEST_BINS = $(TEST_SRCS:tests/%.c=build/tests/%)
TEST_CFLAGS = $(CFLAGS) -DMAELYS_TESTING
ENGINE_HEADERS = $(wildcard include/maelys/*.h src/core/*.h src/compiler/*.h src/public/*.h src/modules/*.h modules/standard/*.h)

$(OBJ_DIR)/%.o: %.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c $< -o $@

libmaelys_datalog.a: $(OBJS)
	ar rcs $@ $^

examples: libmaelys_datalog.a $(EXAMPLES_OBJS) $(EXAMPLES_CHECK)
	$(CC) $(CFLAGS) $(EXAMPLES_OBJS) $(EXAMPLES_CHECK) -L. -lmaelys_datalog $(LIBS) -o $(EXAMPLES_BIN)
	./$(EXAMPLES_BIN)

build/tests/test_maelys_datalog_modules: TEST_EXTRA_SRCS = examples/modules/exact_match.c
build/tests/test_maelys_datalog_modules: TEST_CFLAGS += -pthread
build/tests/test_maelys_datalog_modules: examples/modules/exact_match.c
build/tests/test_maelys_datalog_compiler: TEST_EXTRA_SRCS = examples/modules/arrow_frontend.c examples/modules/naive_backend.c
build/tests/test_maelys_datalog_compiler: examples/modules/arrow_frontend.c examples/modules/naive_backend.c
build/tests/test_maelys_datalog_pipeline: TEST_EXTRA_SRCS = examples/modules/arrow_frontend.c
build/tests/test_maelys_datalog_pipeline: examples/modules/arrow_frontend.c

.PHONY: bench-pipeline
bench-pipeline: build/tests/test_maelys_datalog_pipeline
	./build/tests/test_maelys_datalog_pipeline --bench

build/tests/%: tests/%.c $(SRCS) $(EXAMPLES_SRCS) $(TEST_HELPER_SRCS) $(WASM_TEST_SRCS) $(ENGINE_HEADERS) | build/tests
	$(CC) $(TEST_CFLAGS) -I. -Iinclude $(SRCS) $(EXAMPLES_SRCS) $(TEST_HELPER_SRCS) $(WASM_TEST_SRCS) $(TEST_EXTRA_SRCS) $< -o $@

build/tests:
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
test_maelys_datalog_boundary: build/tests/test_maelys_datalog_boundary
	./build/tests/test_maelys_datalog_boundary

.PHONY: test_maelys_datalog_atom_vocabulary
test_maelys_datalog_atom_vocabulary: build/tests/test_maelys_datalog_atom_vocabulary
	./build/tests/test_maelys_datalog_atom_vocabulary

.PHONY: test_maelys_datalog_determinism
test_maelys_datalog_determinism: build/tests/test_maelys_datalog_determinism
	./build/tests/test_maelys_datalog_determinism

.PHONY: test_maelys_datalog_corpus
test_maelys_datalog_corpus: build/tests/test_maelys_datalog_corpus
	./build/tests/test_maelys_datalog_corpus

clean:
	rm -rf $(BUILD_DIR)
	rm -f libmaelys_datalog.a
	rm -f $(EXAMPLES_BIN)
