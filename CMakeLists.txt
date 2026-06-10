cmake_minimum_required(VERSION 3.16)
project(maelys_datalog VERSION 0.1.0 LANGUAGES C)

set(CMAKE_C_STANDARD 11)

set(ENGINE_SRCS
    policy/maelys_datalog_audit.c
    policy/maelys_datalog_diagnostic.c
    policy/maelys_datalog_domain_registry.c
    policy/maelys_datalog_edb.c
    policy/maelys_datalog_lexer.c
    policy/maelys_datalog_manifest_buffer.c
    policy/maelys_datalog_manifest_file.c
    policy/maelys_datalog_parser.c
    policy/maelys_datalog_policy.c
    policy/maelys_datalog_predicate_registry.c
    policy/maelys_datalog_symbol_table.c)

set(COMMON_SRCS
    common/maelys_sha256.c
    common/maelys_utf8.c
    vendor/yyjson/yyjson.c)

add_library(maelys_datalog STATIC ${ENGINE_SRCS} ${COMMON_SRCS})
target_include_directories(maelys_datalog PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>)
target_compile_options(maelys_datalog PRIVATE -Wall -Wextra)
