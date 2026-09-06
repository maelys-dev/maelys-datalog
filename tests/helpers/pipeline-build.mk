# SPDX-License-Identifier: MPL-2.0
# Only the pipeline test uses observed translation units. All other tests,
# CMake static/shared libraries and install targets use unmodified sources.
PIPELINE_OBSERVED_SRCS = src/core/maelys_datalog_parser.c \
    src/core/maelys_datalog_prepared_session.c \
    src/compiler/maelys_datalog_validate.c src/compiler/maelys_datalog_program.c
PIPELINE_GENERATED_SRCS = $(addprefix $(BUILD_DIR)/pipeline-observed/,$(PIPELINE_OBSERVED_SRCS))
PIPELINE_ENGINE_SRCS = $(filter-out $(PIPELINE_OBSERVED_SRCS),$(ENGINE_SRCS)) $(PIPELINE_GENERATED_SRCS)

$(BUILD_DIR)/pipeline-observed/%.c: %.c tests/helpers/instrument_pipeline.awk tests/helpers/pipeline_counts.h
	@mkdir -p $(dir $@)
	awk -f tests/helpers/instrument_pipeline.awk $< > $@.tmp
	mv $@.tmp $@
