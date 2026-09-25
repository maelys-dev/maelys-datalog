cmake_minimum_required(VERSION 3.16)
project(maelys_datalog VERSION 0.1.0 LANGUAGES C)

include(GNUInstallDirs)

file(STRINGS "${CMAKE_CURRENT_SOURCE_DIR}/VERSION"
     MAELYS_DATALOG_RELEASE_VERSION
     LIMIT_COUNT 1)
message(STATUS "Maelys Datalog release: ${MAELYS_DATALOG_RELEASE_VERSION}")

set(CMAKE_C_STANDARD 11)

# Release artifacts retain DWARF, but never embed temporary checkout/build paths.
# Opt-in so local development keeps its normal debugger source locations.
option(MAELYS_DATALOG_REPRODUCIBLE_PATHS "Map source/build paths in native artifacts" OFF)
if(MAELYS_DATALOG_REPRODUCIBLE_PATHS)
    include(CheckCCompilerFlag)
    check_c_compiler_flag("-ffile-prefix-map=/maelys-probe=." MAELYS_HAS_FILE_PREFIX_MAP)
    if(NOT MAELYS_HAS_FILE_PREFIX_MAP)
        message(FATAL_ERROR "Reproducible native builds require -ffile-prefix-map")
    endif()
    # Clang may canonicalize /var to /private/var on macOS. Map both spellings.
    # The build mapping follows the source mapping for builds inside the source.
    foreach(kind SOURCE BINARY)
        get_filename_component(real_path "${CMAKE_CURRENT_${kind}_DIR}" REALPATH)
        if(kind STREQUAL "SOURCE")
            set(mapped_path "/maelys-datalog")
        else()
            set(mapped_path "/maelys-datalog-build")
        endif()
        add_compile_options("-ffile-prefix-map=${CMAKE_CURRENT_${kind}_DIR}=${mapped_path}"
                            "-ffile-prefix-map=${real_path}=${mapped_path}")
    endforeach()
endif()

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

# The release archive uses these same install rules, selecting SDK + static.
# Keep shared libraries available to ordinary CMake installs, not native tarballs.
if(APPLE)
    # CMake re-runs ranlib after installing a static archive. Apple ranlib's
    # default index timestamp makes two installs differ at the byte level.
    # Scope deterministic archive metadata to this install rule, not its caller.
    install(CODE [[
        set(_maelys_had_zero_ar_date FALSE)
        if(DEFINED ENV{ZERO_AR_DATE})
            set(_maelys_had_zero_ar_date TRUE)
            set(_maelys_zero_ar_date "$ENV{ZERO_AR_DATE}")
        endif()
        set(ENV{ZERO_AR_DATE} 1)
    ]] COMPONENT sdk-static)
endif()
install(TARGETS maelys_datalog
    ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR} COMPONENT sdk-static)
if(APPLE)
    install(CODE [[
        if(_maelys_had_zero_ar_date)
            set(ENV{ZERO_AR_DATE} "${_maelys_zero_ar_date}")
        else()
            unset(ENV{ZERO_AR_DATE})
        endif()
        unset(_maelys_had_zero_ar_date)
        unset(_maelys_zero_ar_date)
    ]] COMPONENT sdk-static)
endif()
install(TARGETS maelys_datalog_shared
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR} COMPONENT sdk-shared
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR} COMPONENT sdk-shared)
install(FILES include/maelys/datalog_advanced.h include/maelys/datalog_details.h include/maelys/datalog.h include/maelys/datalog_builders.h include/maelys/datalog_module.h
    include/maelys/datalog_program.h include/maelys/datalog_backend.h include/maelys/datalog_extension.h
    include/maelys/datalog_window.h include/maelys/datalog_group_window.h
    DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/maelys COMPONENT sdk)
install(FILES LICENSE DESTINATION ${CMAKE_INSTALL_DATADIR}/maelys-datalog COMPONENT sdk)
install(FILES sdk/conformance/maelys_conformance.h sdk/conformance/README.md
    DESTINATION ${CMAKE_INSTALL_DATADIR}/maelys-datalog/conformance COMPONENT sdk)
install(FILES vendor/yyjson/LICENSE DESTINATION ${CMAKE_INSTALL_DATADIR}/maelys-datalog/yyjson COMPONENT sdk)
install(FILES sdk/templates/README.md
    DESTINATION ${CMAKE_INSTALL_DATADIR}/maelys-datalog/templates COMPONENT sdk)
foreach(role frontend backend planner filter)
    set(starter sdk/templates/${role})
    set(destination ${CMAKE_INSTALL_DATADIR}/maelys-datalog/templates/${role})
    install(FILES ${starter}/LICENSE ${starter}/README.md ${starter}/CMakeLists.txt
        DESTINATION ${destination} COMPONENT sdk)
    install(FILES ${starter}/include/extension.h DESTINATION ${destination}/include COMPONENT sdk)
    install(FILES ${starter}/src/extension.c DESTINATION ${destination}/src COMPONENT sdk)
    install(FILES ${starter}/tests/smoke.c DESTINATION ${destination}/tests COMPONENT sdk)
endforeach()

# Archive metadata preserves the existing tarball locations. It is not part
# of a normal prefix installation and contains no second list of headers.
install(FILES LICENSE CHANGELOG.md DESTINATION . COMPONENT release-metadata EXCLUDE_FROM_ALL)
install(FILES vendor/yyjson/LICENSE DESTINATION licenses/yyjson
    COMPONENT release-metadata EXCLUDE_FROM_ALL)

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
    add_executable(test_input_edb_alloc tests/test_maelys_datalog_input_edb_alloc.c)
    target_include_directories(test_input_edb_alloc PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
    target_compile_options(test_input_edb_alloc PRIVATE -UNDEBUG)
    target_link_libraries(test_input_edb_alloc PRIVATE maelys_datalog)
    add_test(NAME input_edb_allocation_contract COMMAND test_input_edb_alloc)
    # Instrument all engine translation units, not only the public wrapper.
    add_library(maelys_allocation_guard STATIC ${ENGINE_SRCS} ${NATIVE_SRCS} ${STANDARD_SRCS})
    target_include_directories(maelys_allocation_guard PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}
        PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
    target_compile_options(maelys_allocation_guard PRIVATE -Wall -Wextra -Werror
        -include ${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/allocation_guard.h)
    if(MAELYS_DATALOG_PROFILE_LARGE)
        target_compile_definitions(maelys_allocation_guard PUBLIC MAELYS_DATALOG_PROFILE_LARGE)
    endif()
    add_executable(test_count tests/test_maelys_datalog_count.c
        tests/helpers/test_log.c tests/helpers/test_framework.c)
    target_include_directories(test_count PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
    target_link_libraries(test_count PRIVATE maelys_datalog)
    add_test(NAME stratified_count COMMAND test_count)
    add_executable(test_group_window tests/test_maelys_datalog_group_window.c)
    target_compile_options(test_group_window PRIVATE -UNDEBUG)
    target_link_libraries(test_group_window PRIVATE maelys_datalog)
    add_test(NAME multi_fact_window COMMAND test_group_window)
    add_executable(test_group_window_alloc tests/test_maelys_datalog_group_window_alloc.c)
    target_include_directories(test_group_window_alloc PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
    target_compile_options(test_group_window_alloc PRIVATE -UNDEBUG)
    target_link_libraries(test_group_window_alloc PRIVATE maelys_allocation_guard)
    add_test(NAME multi_fact_window_allocation_contract COMMAND test_group_window_alloc)
    add_executable(test_window tests/test_maelys_datalog_window.c)
    target_compile_options(test_window PRIVATE -UNDEBUG)
    target_link_libraries(test_window PRIVATE maelys_datalog)
    add_test(NAME last_n_window COMMAND test_window)
    add_executable(test_window_alloc tests/test_maelys_datalog_window_alloc.c)
    target_include_directories(test_window_alloc PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
    target_compile_options(test_window_alloc PRIVATE -UNDEBUG)
    target_link_libraries(test_window_alloc PRIVATE maelys_allocation_guard)
    add_test(NAME last_n_window_allocation_contract COMMAND test_window_alloc)
    add_executable(test_hot_path_alloc tests/test_maelys_datalog_hot_path_alloc.c)
    target_include_directories(test_hot_path_alloc PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
    target_compile_options(test_hot_path_alloc PRIVATE -UNDEBUG)
    target_link_libraries(test_hot_path_alloc PRIVATE maelys_allocation_guard)
    add_test(NAME reference_hot_path_allocation_contract COMMAND test_hot_path_alloc)
    add_executable(test_prepared_explanations tests/test_maelys_datalog_prepared_explanations.c)
    target_include_directories(test_prepared_explanations PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
    target_compile_options(test_prepared_explanations PRIVATE -UNDEBUG)
    target_link_libraries(test_prepared_explanations PRIVATE maelys_allocation_guard)
    add_test(NAME prepared_explanation_allocation_contract COMMAND test_prepared_explanations)
    # This white-box test includes the runtime; the archive supplies other units.
    add_executable(test_session_explanations tests/test_maelys_datalog_session_explanations.c)
    target_include_directories(test_session_explanations PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
    target_compile_options(test_session_explanations PRIVATE -UNDEBUG)
    target_link_libraries(test_session_explanations PRIVATE maelys_allocation_guard)
    add_test(NAME session_explanation_cache_contract COMMAND test_session_explanations)
    add_executable(test_bounded_sort tests/test_maelys_datalog_sort.c)
    target_include_directories(test_bounded_sort PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
    target_compile_options(test_bounded_sort PRIVATE -UNDEBUG)
    target_link_libraries(test_bounded_sort PRIVATE maelys_datalog)
    add_test(NAME bounded_sort_contract COMMAND test_bounded_sort)
    add_executable(test_predicate_builders tests/test_maelys_datalog_predicate_builders.c)
    target_compile_options(test_predicate_builders PRIVATE -UNDEBUG)
    target_link_libraries(test_predicate_builders PRIVATE maelys_datalog)
    add_test(NAME predicate_builders COMMAND test_predicate_builders)
    add_executable(test_query_builders tests/test_maelys_datalog_query_builders.c)
    target_compile_options(test_query_builders PRIVATE -UNDEBUG)
    target_link_libraries(test_query_builders PRIVATE maelys_datalog)
    add_test(NAME query_builders COMMAND test_query_builders)
    # Keep the C-only CMake toolchain contract. make check and the installed
    # SDK gate additionally exercise the C++17 header surface.
    add_test(NAME c11_fact_builder_compilation
        COMMAND sh ${CMAKE_CURRENT_SOURCE_DIR}/tools/check_c11_fact_builders.sh
            ${CMAKE_C_COMPILER} --c-only)
    add_library(example_filter_module STATIC sdk/examples/filter/src/extension.c)
    target_include_directories(example_filter_module PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include)
    add_library(example_compiler_modules STATIC sdk/examples/frontend/src/extension.c sdk/examples/backend/src/extension.c)
    target_include_directories(example_compiler_modules PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include)
    target_compile_options(example_compiler_modules PRIVATE -Wall -Wextra -Werror)
    foreach(linkage IN ITEMS static shared)
        add_executable(test_backend_transaction_${linkage} tests/test_maelys_datalog_backend_transaction.c)
        target_compile_options(test_backend_transaction_${linkage} PRIVATE -Wall -Wextra -Werror)
        if(linkage STREQUAL "static")
            target_link_libraries(test_backend_transaction_${linkage} PRIVATE maelys_datalog)
        else()
            target_link_libraries(test_backend_transaction_${linkage} PRIVATE maelys_datalog_shared)
        endif()
        add_test(NAME backend_transaction_${linkage} COMMAND test_backend_transaction_${linkage})
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
        add_executable(test_context_${linkage} tests/test_maelys_datalog_context.c)
        target_link_libraries(test_context_${linkage} PRIVATE example_compiler_modules Threads::Threads)
        if(linkage STREQUAL "static")
            target_link_libraries(test_context_${linkage} PRIVATE maelys_datalog)
        else()
            target_link_libraries(test_context_${linkage} PRIVATE maelys_datalog_shared)
        endif()
        add_test(NAME context_${linkage} COMMAND test_context_${linkage})
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
