cmake_minimum_required(VERSION 3.16)
project(maelys_datalog VERSION 0.1.0 LANGUAGES C)

set(CMAKE_C_STANDARD 11)

set(ENGINE_SRCS
    src/core/maelys_datalog_audit.c
    src/core/maelys_datalog_diagnostic.c
    src/core/maelys_datalog_domain_registry.c
    src/core/maelys_datalog_edb.c
    src/core/maelys_datalog_introspection.c
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

option(MAELYS_DATALOG_PROFILE_LARGE "Build with the LARGE Datalog size profile" OFF)

if(MAELYS_DATALOG_PROFILE_LARGE)
    target_compile_definitions(maelys_datalog PUBLIC MAELYS_DATALOG_PROFILE_LARGE)
endif()

add_library(maelys_datalog_shared SHARED ${ENGINE_SRCS} ${COMMON_SRCS})
target_include_directories(maelys_datalog_shared PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>)
target_compile_options(maelys_datalog_shared PRIVATE -Wall -Wextra)

if(MAELYS_DATALOG_PROFILE_LARGE)
    target_compile_definitions(maelys_datalog_shared PUBLIC MAELYS_DATALOG_PROFILE_LARGE)
endif()

if(APPLE)
    set(_maelys_rpath "@loader_path")
else()
    set(_maelys_rpath "$ORIGIN")
endif()

set_target_properties(maelys_datalog_shared PROPERTIES
    BUILD_RPATH "${_maelys_rpath}"
    INSTALL_RPATH "${_maelys_rpath}"
    BUILD_WITH_INSTALL_RPATH TRUE
    MACOSX_RPATH TRUE
    INSTALL_NAME_DIR "@rpath")

option(MAELYS_DATALOG_BUILD_EXAMPLES "Build example domains" OFF)

if(MAELYS_DATALOG_BUILD_EXAMPLES)
    add_library(maelys_datalog_examples STATIC
        examples/domains/maelys_datalog_example_domains.c)
    target_include_directories(maelys_datalog_examples PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>)
    target_link_libraries(maelys_datalog_examples maelys_datalog)
endif()

option(MAELYS_DATALOG_BUILD_PYTHON_BINDING "Build the Python cffi shim" OFF)

if(MAELYS_DATALOG_BUILD_PYTHON_BINDING)
    add_library(maelys_py_bind SHARED bindings/python/maelys_py_bind.c)
    target_include_directories(maelys_py_bind PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>)
    target_compile_options(maelys_py_bind PRIVATE -Wall -Wextra)
    target_link_libraries(maelys_py_bind PRIVATE maelys_datalog_shared)
    set_target_properties(maelys_py_bind PROPERTIES
        BUILD_RPATH "${_maelys_rpath}"
        INSTALL_RPATH "${_maelys_rpath}"
        BUILD_WITH_INSTALL_RPATH TRUE
        MACOSX_RPATH TRUE
        INSTALL_NAME_DIR "@rpath")

    add_custom_command(TARGET maelys_py_bind POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E make_directory
                "${CMAKE_CURRENT_SOURCE_DIR}/bindings/python/maelys_datalog"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
                "$<TARGET_FILE:maelys_datalog_shared>"
                "${CMAKE_CURRENT_SOURCE_DIR}/bindings/python/maelys_datalog/"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
                "$<TARGET_FILE:maelys_py_bind>"
                "${CMAKE_CURRENT_SOURCE_DIR}/bindings/python/maelys_datalog/")

    enable_testing()
    add_executable(test_maelys_py_bind
        tests/python/test_maelys_py_bind.c
        tests/helpers/test_framework.c
        tests/helpers/test_log.c)
    target_include_directories(test_maelys_py_bind PRIVATE
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>)
    target_link_libraries(test_maelys_py_bind PRIVATE maelys_py_bind maelys_datalog_shared)
    set_target_properties(test_maelys_py_bind PROPERTIES
        BUILD_RPATH "${_maelys_rpath};$<TARGET_FILE_DIR:maelys_py_bind>;$<TARGET_FILE_DIR:maelys_datalog_shared>")
    add_test(NAME maelys_py_bind COMMAND test_maelys_py_bind)
endif()
