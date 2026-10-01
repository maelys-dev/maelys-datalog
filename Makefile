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


TEST_SRCS = $(wildcard tests/test_*.c)
TEST_BINS = $(TEST_SRCS:tests/%.c=$(BUILD_DIR)/tests/%)
$(BUILD_DIR)/tests/test_maelys_datalog_backend_inputs: TEST_EXTRA_SRCS = sdk/conformance/input_provider.c
$(BUILD_DIR)/tests/test_maelys_datalog_backend_inputs: TEST_CFLAGS += -Isdk/conformance -UNDEBUG
TEST_CFLAGS = $(CFLAGS) -DMAELYS_TESTING
$(BUILD_DIR)/tests/test_maelys_datalog_session_resources: TEST_CFLAGS += -DRESOURCE_ALLOCATION_TEST

$(BUILD_DIR)/tests/test_maelys_datalog_materialization: TEST_CFLAGS += -UNDEBUG
$(BUILD_DIR)/tests/test_maelys_datalog_window $(BUILD_DIR)/tests/test_maelys_datalog_group_window $(BUILD_DIR)/tests/test_maelys_datalog_window_updates: TEST_CFLAGS += -UNDEBUG
ENGINE_HEADERS = $(wildcard include/maelys/*.h src/core/*.h src/compiler/*.h src/public/*.h src/registry/*.h src/runtime/*.h src/runtime/*.inc modules/standard/*.h)

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
$(BUILD_DIR)/tests/test_maelys_datalog_session_recycle: TEST_CFLAGS += -pthread
$(BUILD_DIR)/tests/test_maelys_datalog_context: sdk/examples/frontend/src/extension.c sdk/examples/backend/src/extension.c
$(BUILD_DIR)/tests/test_maelys_datalog_pipeline: TEST_EXTRA_SRCS = sdk/examples/frontend/src/extension.c
$(BUILD_DIR)/tests/test_maelys_datalog_pipeline: sdk/examples/frontend/src/extension.c

# This test includes the EDB implementation with local allocator fault hooks.
# Do not link a second copy of that translation unit into its executable.
$(BUILD_DIR)/tests/test_maelys_datalog_diagnostic_writes: tests/test_maelys_datalog_diagnostic_writes.c $(SRCS) $(ENGINE_HEADERS) | $(BUILD_DIR)/tests
	$(CC) $(TEST_CFLAGS) -UNDEBUG -I. -Iinclude $(filter-out src/core/maelys_datalog_diagnostic.c,$(SRCS)) $< -o $@

$(BUILD_DIR)/tests/test_maelys_datalog_input_edb_alloc: tests/test_maelys_datalog_input_edb_alloc.c $(SRCS) $(ENGINE_HEADERS) | $(BUILD_DIR)/tests
	$(CC) $(TEST_CFLAGS) -UNDEBUG -I. -Iinclude $(filter-out src/runtime/maelys_datalog_input_edb.c,$(SRCS)) $< -o $@

$(BUILD_DIR)/tests/test_maelys_datalog_javascript_transport: tests/test_maelys_datalog_javascript_transport.c tests/fixtures/allocation_guard.h $(SRCS) bindings/javascript/native/transport.c bindings/javascript/native/transport.h $(ENGINE_HEADERS) | $(BUILD_DIR)/tests
	$(CC) $(TEST_CFLAGS) -UNDEBUG -include tests/fixtures/allocation_guard.h $(SRCS) bindings/javascript/native/transport.c $< -o $@

$(BUILD_DIR)/tests/test_maelys_datalog_hot_path_alloc $(BUILD_DIR)/tests/test_maelys_datalog_prepared_explanations $(BUILD_DIR)/tests/test_maelys_datalog_session_resources $(BUILD_DIR)/tests/test_maelys_datalog_session_recycle: $(BUILD_DIR)/tests/%: tests/%.c tests/fixtures/allocation_guard.h $(SRCS) $(ENGINE_HEADERS) | $(BUILD_DIR)/tests
	$(CC) $(TEST_CFLAGS) -UNDEBUG -include tests/fixtures/allocation_guard.h $(SRCS) $< -o $@

$(BUILD_DIR)/tests/test_maelys_datalog_session_explanations: tests/test_maelys_datalog_session_explanations.c tests/fixtures/allocation_guard.h $(SRCS) $(ENGINE_HEADERS) | $(BUILD_DIR)/tests
	$(CC) $(TEST_CFLAGS) -UNDEBUG -include tests/fixtures/allocation_guard.h $(filter-out src/runtime/maelys_datalog_runtime.c,$(SRCS)) $< -o $@

$(BUILD_DIR)/tests/test_maelys_datalog_input_transactions_alloc: tests/test_maelys_datalog_input_transactions_alloc.c tests/fixtures/allocation_guard.h $(SRCS) $(ENGINE_HEADERS) | $(BUILD_DIR)/tests
	$(CC) $(TEST_CFLAGS) -UNDEBUG -include tests/fixtures/allocation_guard.h $(filter-out src/runtime/maelys_datalog_runtime.c,$(SRCS)) $< -o $@

$(BUILD_DIR)/tests/test_maelys_datalog_group_window_alloc: tests/test_maelys_datalog_group_window_alloc.c tests/fixtures/allocation_guard.h $(SRCS) $(ENGINE_HEADERS) | $(BUILD_DIR)/tests
	$(CC) $(TEST_CFLAGS) -UNDEBUG -include tests/fixtures/allocation_guard.h $(filter-out src/runtime/maelys_datalog_group_window.c,$(SRCS)) $< -o $@

$(BUILD_DIR)/tests/test_maelys_datalog_window_alloc: tests/test_maelys_datalog_window_alloc.c tests/fixtures/allocation_guard.h $(SRCS) $(ENGINE_HEADERS) | $(BUILD_DIR)/tests
	$(CC) $(TEST_CFLAGS) -UNDEBUG -include tests/fixtures/allocation_guard.h $(filter-out src/runtime/maelys_datalog_window.c,$(SRCS)) $< -o $@

.PHONY: bench-pipeline
bench-pipeline: $(BUILD_DIR)/tests/test_maelys_datalog_pipeline
	./$(BUILD_DIR)/tests/test_maelys_datalog_pipeline --bench

$(BUILD_DIR)/tests/%: tests/%.c $(SRCS) $(DOMAIN_FIXTURE_SRCS) $(TEST_HELPER_SRCS) $(ENGINE_HEADERS) | $(BUILD_DIR)/tests
	$(CC) $(TEST_CFLAGS) -I. -Iinclude $(SRCS) $(DOMAIN_FIXTURE_SRCS) $(TEST_HELPER_SRCS) $(TEST_EXTRA_SRCS) $< -o $@

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

.PHONY: check check-c11-fact-builders
check-c11-fact-builders:
	sh tools/check_c11_fact_builders.sh "$(CC)" "$(CXX)"

check: test check-version-header check-c11-fact-builders

# The SDK remains the default goal. CLI dependencies are checked only by
# targets that actually build or verify the standalone command.
MAELYS_CLI_DIR = $(MAELYS_DEPENDENCIES_DIR)/maelys-cli
MAELYS_JSON_DIR = $(MAELYS_DEPENDENCIES_DIR)/maelys-json
MAELYS_SPEC_DIR = $(MAELYS_DEPENDENCIES_DIR)/agent-cli-spec
CLI_BUILD = $(BUILD_DIR)/cli
CLI_BINARY = $(BUILD_DIR)/bin/maelys-datalog
CLI_LIB = $(abspath $(BUILD_DIR))/deps/maelys-cli/lib/libmaelys_cli.a
JSON_LIB = $(abspath $(BUILD_DIR))/deps/maelys-json/lib/libmaelys-json.a
CLI_SCHEMA_SRCS = $(wildcard cli/schemas/*.json)
CLI_SCHEMA_SYMBOLS = $(foreach schema,$(CLI_SCHEMA_SRCS),datalog_$(basename $(notdir $(schema)))_schema=$(schema))
CLI_SOURCES = cli/main.c cli/domain.c cli/facts.c
CLI_OBJECTS = $(CLI_SOURCES:cli/%.c=$(CLI_BUILD)/%.o)
CLI_CFLAGS = -std=c11 -Wall -Wextra -Werror -g -I$(MAELYS_CLI_DIR)/include \
	-I$(MAELYS_JSON_DIR)/include -Iinclude -I$(BUILD_DIR)/generated \
	-DDATALOG_CLI_VERSION='"$(shell cat VERSION)"' $(filter -DMAELYS_DATALOG_PROFILE_LARGE,$(CFLAGS))

.PHONY: all check-cli-dependencies check-cli-contract check-json-contract \
	check-spec-contract conformance-check cli-test cli-installed-check
all: libmaelys_datalog.a $(CLI_BINARY)

check-cli-dependencies:
	@test -n "$(MAELYS_DEPENDENCIES_DIR)" || { \
		echo "MAELYS_DEPENDENCIES_DIR is unset; run 'sh scripts/checkout-dependencies.sh DIR' and export the line it prints" >&2; exit 1; }

check-cli-contract: check-cli-dependencies
	@test "$$(git -C "$(MAELYS_CLI_DIR)" rev-parse HEAD)" = "$$(sed -n 2p dependencies/maelys-cli.pin)"
	@test "$$(git -C "$(MAELYS_CLI_DIR)" rev-parse "$$(sed -n 1p dependencies/maelys-cli.pin)^{}")" = "$$(sed -n 2p dependencies/maelys-cli.pin)"
	@git -C "$(MAELYS_CLI_DIR)" diff --quiet HEAD --
	@git -C "$(MAELYS_CLI_DIR)" diff --cached --quiet HEAD --
	@test -z "$$(git -C "$(MAELYS_CLI_DIR)" ls-files --others --exclude-standard)"
	@grep -Fq '#define MAELYS_CLI_ABI 1' "$(MAELYS_CLI_DIR)/include/maelys/cli/version.h"
	@grep -Fq '#define MAELYS_CLI_CONTRACT "agent-cli/v2"' "$(MAELYS_CLI_DIR)/include/maelys/cli/version.h"

check-json-contract: check-cli-dependencies
	@test "$$(git -C "$(MAELYS_JSON_DIR)" rev-parse HEAD)" = "$$(sed -n 2p dependencies/maelys-json.pin)"
	@test "$$(git -C "$(MAELYS_JSON_DIR)" rev-parse "$$(sed -n 1p dependencies/maelys-json.pin)^{}")" = "$$(sed -n 2p dependencies/maelys-json.pin)"
	@git -C "$(MAELYS_JSON_DIR)" diff --quiet HEAD --
	@git -C "$(MAELYS_JSON_DIR)" diff --cached --quiet HEAD --
	@test -z "$$(git -C "$(MAELYS_JSON_DIR)" ls-files --others --exclude-standard)"
	@grep -Fq '#define MAELYS_JSON_ABI_VERSION 2u' "$(MAELYS_JSON_DIR)/include/maelys/json.h"
	@cmp dependencies/maelys-json.pin "$(MAELYS_CLI_DIR)/dependencies/maelys-json.pin"

check-spec-contract: check-cli-dependencies
	@test "$$(git -C "$(MAELYS_SPEC_DIR)" rev-parse HEAD)" = "$$(sed -n 2p dependencies/agent-cli-spec.pin)"
	@test "$$(git -C "$(MAELYS_SPEC_DIR)" rev-parse "$$(sed -n 1p dependencies/agent-cli-spec.pin)^{}")" = "$$(sed -n 2p dependencies/agent-cli-spec.pin)"
	@git -C "$(MAELYS_SPEC_DIR)" diff --quiet HEAD --
	@git -C "$(MAELYS_SPEC_DIR)" diff --cached --quiet HEAD --
	@test -z "$$(git -C "$(MAELYS_SPEC_DIR)" ls-files --others --exclude-standard)"
	@cmp dependencies/agent-cli-spec.pin "$(MAELYS_CLI_DIR)/dependencies/agent-cli-spec.pin"

$(CLI_LIB): | check-cli-contract
	$(MAKE) -C $(MAELYS_CLI_DIR) CPPFLAGS= BUILD=$(abspath $(BUILD_DIR))/deps/maelys-cli $@

$(JSON_LIB): | check-json-contract
	$(MAKE) -C $(MAELYS_JSON_DIR) CPPFLAGS= BUILD=$(abspath $(BUILD_DIR))/deps/maelys-json $@

$(BUILD_DIR)/generated/datalog_schemas.c: $(CLI_SCHEMA_SRCS) | $(CLI_LIB)
	@mkdir -p $(BUILD_DIR)/generated
	$(MAELYS_CLI_DIR)/tools/maelys-cli-embed $(CLI_SCHEMA_SYMBOLS) > $(BUILD_DIR)/generated/datalog_schemas.c

$(BUILD_DIR)/generated/datalog_schemas.h: $(CLI_SCHEMA_SRCS) | $(CLI_LIB)
	@mkdir -p $(BUILD_DIR)/generated
	$(MAELYS_CLI_DIR)/tools/maelys-cli-embed --header $(CLI_SCHEMA_SYMBOLS) > $(BUILD_DIR)/generated/datalog_schemas.h

$(CLI_BUILD)/%.o: cli/%.c cli/reader.h $(BUILD_DIR)/generated/datalog_schemas.h | $(CLI_LIB) $(JSON_LIB)
	@mkdir -p $(dir $@)
	$(CC) $(CLI_CFLAGS) -c $< -o $@

$(CLI_BUILD)/datalog_schemas.o: $(BUILD_DIR)/generated/datalog_schemas.c
	@mkdir -p $(dir $@)
	$(CC) $(CLI_CFLAGS) -c $< -o $@

$(BUILD_DIR)/lib/libmaelys_datalog.a: $(OBJS)
	@mkdir -p $(dir $@)
	ar rcs $@ $^

$(CLI_BINARY): $(CLI_OBJECTS) $(CLI_BUILD)/datalog_schemas.o $(BUILD_DIR)/lib/libmaelys_datalog.a $(CLI_LIB) $(JSON_LIB)
	@mkdir -p $(dir $@)
	$(CC) $^ -o $@

conformance-check: $(CLI_BINARY) check-spec-contract
	python3 $(MAELYS_SPEC_DIR)/conformance/run.py $(abspath $(CLI_BINARY))

cli-test: $(CLI_BINARY) check-spec-contract
	python3 cli/tests/test_cli.py $(abspath $(CLI_BINARY)) $(MAELYS_SPEC_DIR)

cli-installed-check: $(CLI_BINARY)
	PROFILE=$(if $(findstring -DMAELYS_DATALOG_PROFILE_LARGE,$(CFLAGS)),LARGE,SMALL) bash tools/check_cli.sh $(abspath $(BUILD_DIR))

check: check-cli-contract check-json-contract check-spec-contract cli-test \
	conformance-check cli-installed-check

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
