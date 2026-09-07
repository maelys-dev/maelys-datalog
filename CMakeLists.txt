cmake_minimum_required(VERSION 3.16)
project(maelys_datalog VERSION 0.1.0 LANGUAGES C)

include(GNUInstallDirs)

file(STRINGS "${CMAKE_CURRENT_SOURCE_DIR}/VERSION"
     MAELYS_DATALOG_RELEASE_VERSION
     LIMIT_COUNT 1)
message(STATUS "Maelys Datalog release: ${MAELYS_DATALOG_RELEASE_VERSION}")

set(CMAKE_C_STANDARD 11)

# Shared with every Make build; private modules are linked by their consumer.
file(STRINGS build-support/core-sources.txt ENGINE_SRCS)
file(STRINGS build-support/native-sources.txt NATIVE_SRCS)
file(STRINGS build-support/standard-sources.txt STANDARD_SRCS)
add_library(maelys_datalog_standard_filters OBJECT ${STANDARD_SRCS})
target_include_directories(maelys_datalog_standard_filters PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/include)
set_target_properties(maelys_datalog_standard_filters PROPERTIES POSITION_INDEPENDENT_CODE ON)
set(COMMON_SRCS ${NATIVE_SRCS} $<TARGET_OBJECTS:maelys_datalog_standard_filters>)

add_library(maelys_datalog STATIC ${ENGINE_SRCS} ${COMMON_SRCS})
target_include_directories(maelys_datalog PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}
    PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>)
target_compile_options(maelys_datalog PRIVATE -Wall -Wextra)

option(MAELYS_DATALOG_PROFILE_LARGE "Build with the LARGE Datalog size profile" OFF)

if(MAELYS_DATALOG_PROFILE_LARGE)
    target_compile_definitions(maelys_datalog PUBLIC MAELYS_DATALOG_PROFILE_LARGE)
endif()

add_library(maelys_datalog_shared SHARED ${ENGINE_SRCS} ${COMMON_SRCS})
target_include_directories(maelys_datalog_shared PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}
    PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>)
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

install(TARGETS maelys_datalog maelys_datalog_shared
    ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
install(FILES include/maelys/datalog.h include/maelys/datalog_module.h
    include/maelys/datalog_program.h include/maelys/datalog_backend.h
    DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/maelys)
install(FILES LICENSE DESTINATION ${CMAKE_INSTALL_DATADIR}/maelys-datalog)
install(FILES vendor/yyjson/LICENSE DESTINATION ${CMAKE_INSTALL_DATADIR}/maelys-datalog/yyjson)

include(CTest)
if(BUILD_TESTING)
    find_package(Threads REQUIRED)
    add_executable(test_maelys_datalog_public_api
        tests/test_maelys_datalog_public_api.c
        tests/helpers/test_framework.c
        tests/helpers/test_log.c)
    target_include_directories(test_maelys_datalog_public_api PRIVATE
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>)
    target_link_libraries(test_maelys_datalog_public_api PRIVATE maelys_datalog)
    add_test(NAME maelys_datalog_public_api COMMAND test_maelys_datalog_public_api)
    add_library(example_filter_module STATIC examples/sdk/exact_match.c)
    target_include_directories(example_filter_module PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include)
    add_library(example_compiler_modules STATIC examples/sdk/arrow_frontend.c examples/sdk/naive_backend.c)
    target_include_directories(example_compiler_modules PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include)
    target_compile_options(example_compiler_modules PRIVATE -Wall -Wextra -Werror)
    foreach(linkage IN ITEMS static shared)
        add_executable(test_modules_${linkage} tests/test_maelys_datalog_modules.c)
        target_link_libraries(test_modules_${linkage} PRIVATE Threads::Threads)
        if(linkage STREQUAL "static")
            target_link_libraries(test_modules_${linkage} PRIVATE example_filter_module maelys_datalog)
        else()
            target_link_libraries(test_modules_${linkage} PRIVATE example_filter_module maelys_datalog_shared)
        endif()
        add_test(NAME modules_${linkage} COMMAND test_modules_${linkage})
        add_executable(test_compiler_${linkage} tests/test_maelys_datalog_compiler.c)
        if(linkage STREQUAL "static")
            target_link_libraries(test_compiler_${linkage} PRIVATE example_compiler_modules maelys_datalog)
        else()
            target_link_libraries(test_compiler_${linkage} PRIVATE example_compiler_modules maelys_datalog_shared)
        endif()
        add_test(NAME compiler_${linkage} COMMAND test_compiler_${linkage})
        add_executable(test_pipeline_${linkage} tests/test_maelys_datalog_pipeline.c)
        target_include_directories(test_pipeline_${linkage} PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
        if(linkage STREQUAL "static")
            target_link_libraries(test_pipeline_${linkage} PRIVATE example_compiler_modules maelys_datalog)
        else()
            target_link_libraries(test_pipeline_${linkage} PRIVATE example_compiler_modules maelys_datalog_shared)
        endif()
        add_test(NAME pipeline_${linkage} COMMAND test_pipeline_${linkage})
    endforeach()
endif()

option(MAELYS_DATALOG_BUILD_EXAMPLES "Build example domains" OFF)

if(MAELYS_DATALOG_BUILD_EXAMPLES)
    add_library(maelys_datalog_examples STATIC
        tests/fixtures/domains/maelys_datalog_example_domains.c)
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
