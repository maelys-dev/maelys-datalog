# SPDX-License-Identifier: MIT
cmake_minimum_required(VERSION 3.16)
project(maelys_planner_starter LANGUAGES C)

set(MAELYS_SDK_PREFIX "" CACHE PATH "Installed SDK root")
option(MAELYS_SDK_SHARED "Link the smoke test to the shared engine" OFF)
if(NOT MAELYS_SDK_PREFIX)
    message(FATAL_ERROR "Provide -DMAELYS_SDK_PREFIX=/absolute/path/to/installed/sdk")
endif()
find_path(STARTER_HEADERS NAMES maelys/datalog_extension.h
    PATHS "${MAELYS_SDK_PREFIX}/include" NO_DEFAULT_PATH)
set(STARTER_ENGINE maelys_datalog)
if(MAELYS_SDK_SHARED)
    set(STARTER_ENGINE maelys_datalog_shared)
endif()
find_library(STARTER_LIBRARY NAMES ${STARTER_ENGINE}
    PATHS "${MAELYS_SDK_PREFIX}/lib" "${MAELYS_SDK_PREFIX}/lib64" NO_DEFAULT_PATH)
if(NOT STARTER_HEADERS OR NOT STARTER_LIBRARY)
    message(FATAL_ERROR "SDK headers or engine library missing from MAELYS_SDK_PREFIX")
endif()

add_library(extension STATIC src/extension.c)
target_include_directories(extension PUBLIC include "${STARTER_HEADERS}")
set_target_properties(extension PROPERTIES C_STANDARD 11 C_STANDARD_REQUIRED YES C_EXTENSIONS NO)
target_compile_options(extension PRIVATE -Wall -Wextra -Werror)

add_executable(smoke tests/smoke.c)
set_target_properties(smoke PROPERTIES C_STANDARD 11 C_STANDARD_REQUIRED YES C_EXTENSIONS NO)
target_compile_options(smoke PRIVATE -Wall -Wextra -Werror)
target_link_libraries(smoke PRIVATE extension "${STARTER_LIBRARY}")
enable_testing()
add_test(NAME planner_starter COMMAND smoke)
