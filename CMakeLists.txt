cmake_minimum_required(VERSION 3.16)
project(maelys_datalog VERSION 0.1.0 LANGUAGES C)

set(CMAKE_C_STANDARD 11)

set(ENGINE_SRCS
    src/core/maelys_datalog_audit.c
    src/core/maelys_datalog_diagnostic.c
    src/core/maelys_datalog_domain_registry.c
    src/core/maelys_datalog_edb.c
    src/core/maelys_datalog_lexer.c
    src/core/maelys_datalog_parser.c
    src/core/maelys_datalog_predicate_registry.c
    src/core/maelys_datalog_ruleset.c
    src/core/maelys_datalog_ruleset_canonical.c
    src/core/maelys_datalog_solver.c
    src/core/maelys_datalog_symbol_table.c
    src/core/maelys_datalog_decision.c
    src/manifest/maelys_datalog_manifest_buffer.c
    src/manifest/maelys_datalog_manifest_file.c)

set(COMMON_SRCS
    common/maelys_sha256.c
    common/maelys_utf8.c
    vendor/yyjson/yyjson.c)

add_library(maelys_datalog STATIC ${ENGINE_SRCS} ${COMMON_SRCS})
target_include_directories(maelys_datalog PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>)
target_compile_options(maelys_datalog PRIVATE -Wall -Wextra)

option(MAELYS_DATALOG_BUILD_EXAMPLES "Build example domains" OFF)

if(MAELYS_DATALOG_BUILD_EXAMPLES)
    add_library(maelys_datalog_examples STATIC
        examples/domains/maelys_datalog_example_domains.c)
    target_include_directories(maelys_datalog_examples PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>)
    target_link_libraries(maelys_datalog_examples maelys_datalog)
endif()
