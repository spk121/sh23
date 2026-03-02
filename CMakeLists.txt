cmake_minimum_required(VERSION 3.16)

set(CMAKE_COLOR_DIAGNOSTICS OFF)

project(mgsh LANGUAGES C)

# ============================================================================
# Build Configuration Strategy
# ============================================================================
#
# API Selection (API_TYPE):
#   POSIX:  POSIX API + visibility attributes (Linux/macOS development)
#   UCRT:   Windows UCRT API + __declspec (Windows development)
#   ISO_C:  Strict ISO C only (portability verification)
#
# Library Strategy:
#   Debug (POSIX + Linux):    Shared libs with --no-undefined (catches layering violations)
#   Debug (other):            Static libs (no layering validation available)
#   Release (all):            Static libs (lean binary, no exposed symbols)
#
# Symbol Visibility:
#   POSIX/UCRT: Controlled via attributes (hidden by default)
#   ISO_C:      Default visibility (no attributes allowed)
#
# Recommended workflow:
#   1. Development: API_TYPE=POSIX, CMAKE_BUILD_TYPE=Debug (on Linux for best validation)
#   2. Testing:     API_TYPE=ISO_C to verify portability
#   3. Release:     API_TYPE=POSIX/UCRT, CMAKE_BUILD_TYPE=Release
# ============================================================================

# The goal is C23, but on MSVC the best we can do it call its "/std:clatest" option.
# set(CMAKE_C_STANDARD 23)
set(CMAKE_C_STANDARD_REQUIRED ON)

# Export compile commands for IDE integration
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# ============================================================================
# Build type configuration
# ============================================================================

# Set default build type if not specified
if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE "Release" CACHE STRING "Build type (Debug, Release, RelWithDebInfo, MinSizeRel)" FORCE)
    set_property(CACHE CMAKE_BUILD_TYPE PROPERTY STRINGS "Debug" "Release" "RelWithDebInfo" "MinSizeRel")
endif()

message(STATUS "Build type: ${CMAKE_BUILD_TYPE}")

# ============================================================================
# API selection option
# ============================================================================

set(API_TYPE "UCRT" CACHE STRING "Choose the API type: POSIX, UCRT, or ISO_C")
set_property(CACHE API_TYPE PROPERTY STRINGS "POSIX" "UCRT" "ISO_C")

# Define the appropriate API macro based on selection
if(API_TYPE STREQUAL "POSIX")
    add_compile_definitions(POSIX_API)
    message(STATUS "Building with POSIX API")
elseif(API_TYPE STREQUAL "UCRT")
    add_compile_definitions(UCRT_API)
    message(STATUS "Building with UCRT API")
elseif(API_TYPE STREQUAL "ISO_C")
    add_compile_definitions(ISO_C)
    message(STATUS "Building with ISO C (no platform-specific API)")
else()
    message(FATAL_ERROR "Invalid API_TYPE: ${API_TYPE}. Must be POSIX, UCRT, or ISO_C")
endif()

# ============================================================================
# Symbol visibility configuration
# ============================================================================

if(API_TYPE STREQUAL "ISO_C")
    # ISO C mode: cannot use compiler-specific visibility attributes
    # On Windows DLLs, we'll use default exports (but we build static anyway)
    message(STATUS "ISO C mode: using default symbol visibility")

    # Don't set CMAKE_C_VISIBILITY_PRESET in ISO_C mode
else()
    # POSIX or UCRT: use hidden visibility by default
    set(CMAKE_C_VISIBILITY_PRESET hidden)
    set(CMAKE_VISIBILITY_INLINES_HIDDEN YES)
    message(STATUS "Using hidden symbol visibility by default")
endif()

# Define build type macros
add_compile_definitions(
    $<$<CONFIG:Debug>:SH23_DEBUG_BUILD>
    $<$<CONFIG:Release>:SH23_RELEASE_BUILD>
)

# ============================================================================
# Compiler options
# ============================================================================

option(ENABLE_COVERAGE "Enable code coverage" OFF)
option(ENABLE_SANITIZERS "Enable Address and Undefined Behavior Sanitizers" OFF)

if(MSVC)
    # MSVC: Maximum useful warnings + treat as errors
    add_compile_options(
        /std:clatest    # Latest C standard (C23-ish)
        # /Wall           # Enable almost all warnings (stronger than /W4)
        /W3 		    # Level 3 warnings (practical balance)
        # /WX             # Treat warnings as errors
        /we4028         # Treat formal parameter unreferenced warnings as errors
        /we4029         # Treat redefinition warnings as errors
        /we4133         # Treat incompatible pointer type warnings as errors
        /wd5045         # Disable Spectre mitigation warning
        /wd6011         # Disable warning about dereferencing NULL pointer
        /wd4820         # Disable padding warning
        /wd4061         # Disable enum not handled in switch warning
        /Zi             # Debug info
    )

    # Optimization based on build type
    add_compile_options(
        $<$<CONFIG:Debug>:/Od>
        $<$<CONFIG:Release>:/O2>
        $<$<CONFIG:RelWithDebInfo>:/O2>
        $<$<CONFIG:MinSizeRel>:/Os>
    )

    # Optional: Disable particularly noisy /Wall warnings if needed
    # add_compile_options(/wdXXXX)  # e.g., /wd4201 for nonstandard extension anonymous struct/union

    if(ENABLE_COVERAGE)
        message(FATAL_ERROR "Code coverage is not supported with MSVC")
    elseif(ENABLE_SANITIZERS)
        message(FATAL_ERROR "Sanitizers are not supported with MSVC")
    endif()
else()
    # Common warnings for GCC and Clang
    add_compile_options(
        -std=c2x
        -Wall
        -Wextra
        -Wpedantic
        -Wshadow
        -Wformat=2
        -Wformat-signedness
        -Wformat-security
        -Wconversion
        -Wsign-conversion
        -Wnull-dereference
        -Wdouble-promotion
        -Wstrict-prototypes
        -Wmissing-prototypes
        -Wold-style-definition
        -Wmissing-declarations
        -Wcast-align
        -Wcast-qual
        -Wunused
        -Wvla                   # Warn on variable-length arrays (often a bad idea)
        -Wswitch-default
        -Wswitch-enum
        -Wfloat-equal
        -Wundef
    )

    # Debug info based on build type
    add_compile_options(
        $<$<CONFIG:Debug>:-g3>
        $<$<CONFIG:RelWithDebInfo>:-g>
    )

    if(CI)
        add_compile_options(-Werror)
    endif()

    # Compiler-specific extras
    if(CMAKE_C_COMPILER_ID STREQUAL "GNU")
        add_compile_options(
            -fanalyzer          # GCC static analyzer (very useful, low false positives)
            -Wlogical-op
            -Warith-conversion
            -Wduplicated-cond
            -Wduplicated-branches
        )
    elseif(CMAKE_C_COMPILER_ID STREQUAL "Clang")
        # Clang already catches many GCC-specific things; add a few extras if desired
        # add_compile_options(-Weverything)  # Only for experimentation – too noisy!
    else()
        message(WARNING "Unknown compiler – using generic non-MSVC flags")
    endif()

    # Coverage, sanitizers, or normal optimization
    if(ENABLE_COVERAGE)
        if(NOT CMAKE_C_COMPILER_ID MATCHES "GNU|Clang")
            message(FATAL_ERROR "Coverage only supported with GCC or Clang")
        endif()

        add_compile_options(-O0 --coverage)
        add_link_options(--coverage)
        message(STATUS "Code coverage enabled (-O0 --coverage)")
    elseif(ENABLE_SANITIZERS)
        if(NOT CMAKE_C_COMPILER_ID MATCHES "GNU|Clang")
            message(FATAL_ERROR "Sanitizers only supported with GCC or Clang")
        endif()
        add_compile_options(-O1 -fsanitize=address,undefined -fno-omit-frame-pointer)
        add_link_options(-fsanitize=address,undefined)
        message(STATUS "Sanitizers enabled (-fsanitize=address,undefined)")
    else()
        # Use build-type-appropriate optimization
        add_compile_options(
            $<$<CONFIG:Debug>:-O0>
            $<$<CONFIG:Release>:-O2>
            $<$<CONFIG:RelWithDebInfo>:-O2>
            $<$<CONFIG:MinSizeRel>:-Os>
        )
    endif()
endif()


# ============================================================================
# Library type and strict linking configuration
# ============================================================================

# Determine library type based on build configuration
if(CMAKE_BUILD_TYPE STREQUAL "Debug" OR ENABLE_COVERAGE OR ENABLE_SANITIZERS)
    # Debug/testing: use shared libs to catch layering violations where possible
    if(UNIX AND NOT APPLE AND API_TYPE STREQUAL "POSIX")
        # Optimal case: Linux + POSIX = shared libs with strict validation
        set(SH23_LIBRARY_TYPE SHARED)
        message(STATUS "Building shared libraries for debug/testing with strict linking")
    else()
        # All other cases: static libraries (no layering validation)
        set(SH23_LIBRARY_TYPE STATIC)
        if(API_TYPE STREQUAL "ISO_C")
            message(STATUS "Building static libraries (ISO C mode: no layering validation)")
        else()
            message(STATUS "Building static libraries (no --no-undefined on this platform)")
        endif()
    endif()
else()
    # Release: always static
    set(SH23_LIBRARY_TYPE STATIC)
    message(STATUS "Building static libraries for release")
endif()

# Helper function to enable strict no-undefined checking
function(enable_strict_linking target)
    # Only apply strict linking when:
    # 1. Building shared libraries AND
    # 2. Using POSIX API (not ISO_C) AND
    # 3. On a platform that supports --no-undefined
    if(SH23_LIBRARY_TYPE STREQUAL SHARED AND API_TYPE STREQUAL "POSIX")
        target_link_options(${target} PRIVATE -Wl,--no-undefined)
        message(STATUS "  ${target}: strict linking enabled")
    endif()
endfunction()

# ============================================================================
# Library 1: sh23base - Base utilities
# ============================================================================
set(SH23BASE_SOURCES
    src/getopt.c
    src/getopt.h
    src/getopt_string.c
    src/getopt_string.h
    src/glob_util.c
    src/glob_util.h
    src/lib.c
    src/lib.h
    src/logging.c
    src/logging.h
    src/pattern_removal.c
    src/pattern_removal.h
    src/string_t.c
    src/string_t.h
    src/string_list.c
    src/string_list.h
    src/xalloc.c
    src/xalloc.h
)

add_library(sh23base ${SH23_LIBRARY_TYPE} ${SH23BASE_SOURCES})
target_include_directories(sh23base PUBLIC src)
enable_strict_linking(sh23base)

# ============================================================================
# Library 2: sh23store - Data stores (depends on sh23base)
# ============================================================================
set(SH23STORE_SOURCES
    src/alias.c
    src/alias.h
    src/alias_array.c
    src/alias_array.h
    src/alias_store.c
    src/alias_store.h
    src/ast.c
    src/ast.h
    src/fd_table.c
    src/fd_table.h
    src/func_map.c
    src/func_map.h
    src/func_store.c
    src/func_store.h
    src/gnode.c
    src/gnode.h
    src/gprint.c
    src/gprint.h
    src/job_store.c
    src/job_store.h
    src/sig_act.c
    src/sig_act.h
    src/token.c
    src/token.h
    src/token_array.c
    src/token_array.h
    src/trap_store.c
    src/trap_store.h
    src/variable_map.c
    src/variable_map.h
    src/variable_store.c
    src/variable_store.h
)

add_library(sh23store ${SH23_LIBRARY_TYPE} ${SH23STORE_SOURCES})
target_include_directories(sh23store PUBLIC src)
target_link_libraries(sh23store PUBLIC sh23base)
enable_strict_linking(sh23store)

# ============================================================================
# Library 3: sh23logic - Core shell logic (depends on sh23store and sh23base)
# ============================================================================
set(SH23LOGIC_SOURCES
    src/arithmetic.c
    src/arithmetic.h
    src/builtins.c
    src/builtins.h
    src/exec.c
    src/exec.h
    src/exec_command.c
    src/exec_command.h
    #src/exec_compound.c
    #src/exec_compound.h
    src/exec_expander.c
    src/exec_expander.h
    src/exec_frame.c
    src/exec_frame.h
    src/exec_internal.h
    src/exec_redirect.c
    src/exec_redirect.h
    src/frame.c
    src/frame.h
    src/job_store.c
    src/job_store.h
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
    src/lexer_t.h
    src/lexer_priv_t.h
    src/lower.c
    src/lower.h
    src/parser.c
    src/parser.h
    src/positional_params.c
    src/positional_params.h
    src/shell.c
    src/shell.h
    src/tokenizer.c
    src/tokenizer.h
)

add_library(sh23logic ${SH23_LIBRARY_TYPE} ${SH23LOGIC_SOURCES})
target_include_directories(sh23logic PUBLIC src)
target_link_libraries(sh23logic PUBLIC sh23store sh23base)
enable_strict_linking(sh23logic)

# ============================================================================
# Library 4: sh23interface - Public interface (depends on sh23logic, sh23store, sh23base)
# ============================================================================

add_library(sh23interface INTERFACE)
target_link_libraries(sh23interface INTERFACE sh23logic sh23store sh23base)

# ============================================================================
# Executable: mgsh
# ============================================================================

add_executable(mgsh src/main.c)
target_link_libraries(mgsh PRIVATE sh23interface)
set_target_properties(mgsh PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin
)

# ============================================================================
# Testing configuration
# ============================================================================

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
    test/mgsh/test_xalloc_ctest.c
    test/mgsh/test_getopt_ctest.c
    test/mgsh/test_string_ctest.c
)

# Tests that depend on sh23store (and sh23base)
set(SH23STORE_TEST_SOURCES
    test/mgsh/test_alist_ctest.c
    test/mgsh/test_ast_ctest.c
    test/mgsh/test_sig_act_ctest.c
)

# Tests that depend on full sh23logic (all libraries)
set(SH23LOGIC_TEST_SOURCES
    test/mgsh/test_lexer_quotes_ctest.c
    test/mgsh/test_lexer_param_exp_ctest.c
    test/mgsh/test_lexer_cmd_subst_ctest.c
    test/mgsh/test_lexer_arith_exp_ctest.c
    test/mgsh/test_lexer_heredoc_ctest.c
    test/mgsh/test_parser_gnode_ctest.c
    test/mgsh/test_parser_ctest.c
    test/mgsh/test_ast_heredoc_ctest.c
    test/mgsh/test_tokenizer_ctest.c
    # test/mgsh/test_expander_ctest.c
    test/mgsh/test_exec_ctest.c
)

# Build sh23base tests
foreach(test_src ${SH23BASE_TEST_SOURCES})
    get_filename_component(test_name ${test_src} NAME_WE)

    add_executable(${test_name} ${test_src} $<TARGET_OBJECTS:ctest_obj>)
    # Test-specific C code can be conditionally compiled with #ifdef IN_CTEST
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

# ============================================================================
# Code coverage target
# ============================================================================

if(ENABLE_COVERAGE)
    find_program(GENHTML genhtml)
    find_program(LCOV lcov)

    if(GENHTML AND LCOV)
        add_custom_target(coverage
            COMMAND ${CMAKE_CTEST_COMMAND}  # Run tests
            COMMAND ${LCOV} --capture --directory . --output-file coverage.raw.info
            COMMAND ${LCOV} --remove coverage.raw.info '/usr/*' '*/test/*' '*/src/main.c' '*/src/getopt.c' --output-file coverage.info
            COMMAND ${LCOV} --extract coverage.info '*/src/*.c' --output-file coverage.filtered.info
            COMMAND ${GENHTML} coverage.filtered.info --output-directory coverage_report --title "mgsh Coverage" --branch-coverage --legend
            COMMENT "Generating coverage report in coverage_report/"
            WORKING_DIRECTORY ${CMAKE_BINARY_DIR}
        )
    endif()
endif()

# ============================================================================
# Fun emergency target 😅
# ============================================================================
add_custom_target(wtf
    COMMAND ${CMAKE_COMMAND} -E echo ""
    COMMAND ${CMAKE_COMMAND} -E echo "🚀  You've got this! Everything is going to be fine. 🌟"
    COMMAND ${CMAKE_COMMAND} -E echo ""
    COMMAND ${CMAKE_COMMAND} -E echo "💪 Keep going — the code will bend to your will eventually. 🔥"
    COMMAND ${CMAKE_COMMAND} -E echo "☕ Take a deep breath, maybe grab a coffee, and come back stronger. ✨"
    COMMAND ${CMAKE_COMMAND} -E echo ""
    COMMENT "Running the WTF target — motivational support activated!"
)

