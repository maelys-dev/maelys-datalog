CC = clang
CFLAGS = -Wall -Wextra -g -I.
LIBS =
BUILD_DIR ?= build
OBJ_DIR = $(BUILD_DIR)/obj

ENGINE_SRCS = \
	src/core/maelys_datalog_audit.c \
	src/core/maelys_datalog_diagnostic.c \
	src/core/maelys_datalog_domain_registry.c \
	src/core/maelys_datalog_edb.c \
	src/core/maelys_datalog_introspection.c \
	src/core/maelys_datalog_lexer.c \
	src/core/maelys_datalog_parser.c \
	src/core/maelys_datalog_predicate_registry.c \
	src/core/maelys_datalog_ruleset.c \
	src/core/maelys_datalog_ruleset_canonical.c \
	src/core/maelys_datalog_solver.c \
	src/core/maelys_datalog_symbol_table.c \
	src/core/maelys_datalog_decision.c \
	src/manifest/maelys_datalog_manifest_buffer.c \
	src/manifest/maelys_datalog_manifest_file.c

COMMON_SRCS = \
	common/maelys_sha256.c \
	common/maelys_utf8.c \
	vendor/yyjson/yyjson.c

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

$(OBJ_DIR)/%.o: %.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c $< -o $@

libmaelys_datalog.a: $(OBJS)
	ar rcs $@ $^

examples: libmaelys_datalog.a $(EXAMPLES_OBJS) $(EXAMPLES_CHECK)
	$(CC) $(CFLAGS) $(EXAMPLES_OBJS) $(EXAMPLES_CHECK) -L. -lmaelys_datalog $(LIBS) -o $(EXAMPLES_BIN)
	./$(EXAMPLES_BIN)

build/tests/%: tests/%.c $(SRCS) $(EXAMPLES_SRCS) $(TEST_HELPER_SRCS) $(WASM_TEST_SRCS) | build/tests
	$(CC) $(TEST_CFLAGS) -I. $(SRCS) $(EXAMPLES_SRCS) $(TEST_HELPER_SRCS) $(WASM_TEST_SRCS) $< -o $@

build/tests:
	mkdir -p $@

test: $(TEST_BINS)
	@set -e; for b in $(TEST_BINS); do echo "--- $$b ---"; ./$$b; done

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
