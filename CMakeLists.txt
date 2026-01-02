cmake_minimum_required(VERSION 3.16)

set(CMAKE_COLOR_DIAGNOSTICS OFF)

project(mgsh LANGUAGES C)

# Export compile commands for IDE integration
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
# API selection option
set(API_TYPE "ISO_C" CACHE STRING "Choose the API type: POSIX, UCRT, or ISO_C")
set_property(CACHE API_TYPE PROPERTY STRINGS "POSIX" "UCRT" "ISO_C")

# Define the appropriate API macro based on selection
if(API_TYPE STREQUAL "POSIX")
    add_compile_definitions(POSIX_API)
    message(STATUS "Building with POSIX API")
elseif(API_TYPE STREQUAL "UCRT")
    add_compile_definitions(UCRT_API)
    message(STATUS "Building with UCRT API")
elseif(API_TYPE STREQUAL "ISO_C")
    message(STATUS "Building with ISO C (no platform-specific API)")
else()
    message(FATAL_ERROR "Invalid API_TYPE: ${API_TYPE}. Must be POSIX, UCRT, or ISO_C")
endif()

if(MSVC)
    add_compile_options(/std:clatest /W4 /Od /Zi)
else()
    add_compile_options(
        -Wall                          # Enable common warnings
        -Wextra                        # Extra warnings
        -Wpedantic                     # Strict ISO C compliance
        -Wshadow                       # Warn about variable shadowing
        -Wformat=2                     # Enhanced format string checking
        -Wconversion                   # Warn on implicit type conversions
        -Wsign-conversion              # Warn on sign conversions
        -Wnull-dereference             # Warn about null pointer dereferences
        -Wdouble-promotion             # Warn on float to double promotion
        -Wstrict-prototypes            # Require function prototypes
        -Wmissing-prototypes           # Warn if global function has no prototype
        -Wold-style-definition         # Warn about old-style function definitions
        -Wmissing-declarations         # Warn if a global function is defined without a declaration
        -Wcast-align                   # Warn about pointer casts with alignment issues
        -Wcast-qual                    # Warn about casts that discard qualifiers
        -Wunused                       # Warn on unused entities
        -fanalyzer                     # Enable static analysis (GCC 10+)
        -g3                            # Maximum debug info
        -O2                            # Optimization level

        )
endif()

# ============================================================================
# Library type configuration
# ============================================================================
if(UNIX AND NOT APPLE)
    set(SH23_LIBRARY_TYPE SHARED)
    # Enforce no undefined symbols in shared libraries (like Windows DLLs)
    add_link_options(-Wl,--no-undefined)
    message(STATUS "Building shared libraries with strict linking")
else()
    set(SH23_LIBRARY_TYPE STATIC)
    message(STATUS "Building static libraries")
endif()

# ============================================================================
# Library 1: sh23base - Base utilities
# ============================================================================
set(SH23BASE_SOURCES
    src/getopt.c
    src/getopt.h
    src/lib.c
    src/lib.h
    src/logging.c
    src/logging.h
    src/string_t.c
    src/string_t.h
    src/xalloc.c
    src/xalloc.h
)

add_library(sh23base ${SH23_LIBRARY_TYPE} ${SH23BASE_SOURCES})

# ============================================================================
# Library 2: sh23store - Data stores (depends on sh23base)
# ============================================================================
set(SH23STORE_SOURCES
    src/alias.c
    src/alias.h
    src/alias_store.c
    src/alias_store.h
    src/alias_array.c
    src/alias_array.h
    src/func_map.c
    src/func_map.h
    src/func_store.c
    src/func_store.h
    src/gnode.c
    src/gnode.h
    src/gprint.c
    src/gprint.h    
    src/variable_map.c
    src/variable_map.h
    src/variable_store.c
    src/variable_store.h
    src/token.c
    src/token.h
    src/token_array.c
    src/token_array.h
    src/ast.c
    src/ast.h
    src/pattern_removal.c
    src/pattern_removal.h
)

add_library(sh23store ${SH23_LIBRARY_TYPE} ${SH23STORE_SOURCES})
target_include_directories(sh23store PUBLIC src)
target_link_libraries(sh23store PUBLIC sh23base)

# ============================================================================
# Library 3: sh23logic - Core shell logic (depends on sh23store and sh23base)
# ============================================================================
set(SH23LOGIC_SOURCES
    src/arithmetic.c
    src/arithmetic.h
    src/lower.c
    src/lower.h
    src/lexer.c
    src/lexer.h
    src/lexer_arith_exp.c
    src/lexer_arith_exp.h
    src/lexer_cmd_subst.c
    src/lexer_cmd_subst.h
    src/lexer_heredoc.c
    src/lexer_heredoc.h
    src/lexer_normal.c
    src/lexer_normal.h
    src/lexer_squote.c
    src/lexer_squote.h
    src/lexer_dquote.c
    src/lexer_dquote.h
    src/lexer_param_exp.c
    src/lexer_param_exp.h
    src/tokenizer.c
    src/tokenizer.h
    src/parser.c
    src/parser.h
    src/executor.c
    src/executor.h
    src/expander.c
    src/expander.h
    src/shell.c
    src/shell.h
    src/positional_params.c
    src/positional_params.h
)

add_library(sh23logic ${SH23_LIBRARY_TYPE} ${SH23LOGIC_SOURCES})
target_include_directories(sh23logic PUBLIC src)
target_link_libraries(sh23logic PUBLIC sh23store sh23base)

add_library(sh23interface INTERFACE)
target_link_libraries(sh23interface INTERFACE sh23logic sh23store sh23base)


add_executable(mgsh src/main.c)
target_link_libraries(mgsh PRIVATE sh23interface)
set_target_properties(mgsh PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin
)

enable_testing()

# Object library for the shared CTest framework used by all shell tests
add_library(ctest_obj OBJECT
    test/ctest/ctest.c
    test/ctest/ctest.h
)
target_include_directories(ctest_obj PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/test/ctest
)

# Tests that only depend on sh23base
set(SH23BASE_TEST_SOURCES
    test/test_xalloc_ctest.c
    test/mgsh/test_getopt_ctest.c
    test/mgsh/test_string_ctest.c
)

# Tests that depend on sh23store (and sh23base)
set(SH23STORE_TEST_SOURCES
    test/mgsh/test_alist_ctest.c
    test/mgsh/test_ast_ctest.c
    test/mgsh/test_ast_heredoc_ctest.c
)

# Tests that depend on full sh23logic (all libraries)
set(SH23LOGIC_TEST_SOURCES
    test/mgsh/test_lexer_quotes_ctest.c
    test/mgsh/test_lexer_param_exp_ctest.c
    test/mgsh/test_lexer_cmd_subst_ctest.c
    test/mgsh/test_lexer_arith_exp_ctest.c
    test/mgsh/test_lexer_heredoc_ctest.c
    test/mgsh/test_parser_gnode_ctest.c
    test/mgsh/test_tokenizer_ctest.c
    test/mgsh/test_expander_ctest.c
    test/mgsh/test_executor_ctest.c
)

# Build sh23base tests
foreach(test_src ${SH23BASE_TEST_SOURCES})
    get_filename_component(test_name ${test_src} NAME_WE)

    add_executable(${test_name} ${test_src} $<TARGET_OBJECTS:ctest_obj>)
    target_compile_definitions(${test_name} PRIVATE IN_CTEST)
    target_include_directories(${test_name} PRIVATE
        src
        ${CMAKE_CURRENT_SOURCE_DIR}/test/ctest
    )
    target_link_libraries(${test_name} PRIVATE sh23base)

    set_target_properties(${test_name} PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/test
    )

    add_test(NAME ${test_name}
        COMMAND ${test_name}
        WORKING_DIRECTORY ${CMAKE_BINARY_DIR}/test
    )

    set_tests_properties(${test_name} PROPERTIES
        FAIL_REGULAR_EXPRESSION "not ok|\\b(Bail out!|Bailout!)\\b"
        PASS_REGULAR_EXPRESSION "ok"
    )
endforeach()

# Build sh23store tests
foreach(test_src ${SH23STORE_TEST_SOURCES})
    get_filename_component(test_name ${test_src} NAME_WE)

    add_executable(${test_name} ${test_src} $<TARGET_OBJECTS:ctest_obj>)
    target_compile_definitions(${test_name} PRIVATE IN_CTEST)
    target_include_directories(${test_name} PRIVATE
        src
        ${CMAKE_CURRENT_SOURCE_DIR}/test/ctest
    )
    target_link_libraries(${test_name} PRIVATE sh23store sh23base)

    set_target_properties(${test_name} PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/test
    )

    add_test(NAME ${test_name}
        COMMAND ${test_name}
        WORKING_DIRECTORY ${CMAKE_BINARY_DIR}/test
    )

    set_tests_properties(${test_name} PROPERTIES
        FAIL_REGULAR_EXPRESSION "not ok|\\b(Bail out!|Bailout!)\\b"
        PASS_REGULAR_EXPRESSION "ok"
    )
endforeach()

# Build sh23logic tests
foreach(test_src ${SH23LOGIC_TEST_SOURCES})
    get_filename_component(test_name ${test_src} NAME_WE)

    add_executable(${test_name} ${test_src} $<TARGET_OBJECTS:ctest_obj>)
    target_compile_definitions(${test_name} PRIVATE IN_CTEST)
    target_include_directories(${test_name} PRIVATE
        src
        ${CMAKE_CURRENT_SOURCE_DIR}/test/ctest
    )
    target_link_libraries(${test_name} PRIVATE sh23interface)

    set_target_properties(${test_name} PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/test
    )

    add_test(NAME ${test_name}
        COMMAND ${test_name}
        WORKING_DIRECTORY ${CMAKE_BINARY_DIR}/test
    )

    set_tests_properties(${test_name} PROPERTIES
        FAIL_REGULAR_EXPRESSION "not ok|\\b(Bail out!|Bailout!)\\b"
        PASS_REGULAR_EXPRESSION "ok"
    )
endforeach()
