cmake_minimum_required(VERSION 3.16)
project(mgsh LANGUAGES C)

if(MSVC)
    add_compile_options(/std:clatest /W4 /Od /Zi)
else()
    add_compile_options(-Wall -Wextra -g3 -O0)
endif()

# Collect all sources except main.c
set(MGSH_LIB_SOURCES
    src/alias.c
    src/alias.h
    src/alias_store.c
    src/alias_store.h
    src/alias_array.c
    src/alias_array.h
    src/arithmetic.c
    src/arithmetic.h
    src/logging.c
    src/logging.h
    src/string_t.c
    src/string_t.h
    src/xalloc.c
    src/xalloc.h
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
    src/token.c
    src/token.h
    src/tokenizer.c
    src/tokenizer.h
    src/ast.c
    src/ast.h
    src/parser.c
    src/parser.h
    src/executor.c
    src/executor.h
    src/expander.c
    src/expander.h
    src/variable.c
    src/variable.h
    src/variable_array.c
    src/variable_array.h
    src/variable_store.c
    src/variable_store.h
)

add_library(mgsh_lib STATIC ${MGSH_LIB_SOURCES})
target_include_directories(mgsh_lib PUBLIC src)

add_executable(mgsh src/main.c)
target_link_libraries(mgsh PRIVATE mgsh_lib)
set_target_properties(mgsh PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin
)

enable_testing()

# Object library for the shared CTest framework used by all ed tests
add_library(ctest_obj OBJECT
    test/ctest/ctest.c
    test/ctest/ctest.h
)
target_include_directories(ctest_obj PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/test/ctest
)

set(MGSH_TEST_SOURCES
    test/mgsh/test_alist_ctest.c
    test/mgsh/test_lexer_quotes_ctest.c
    test/mgsh/test_lexer_param_exp_ctest.c
    test/mgsh/test_lexer_cmd_subst_ctest.c
    test/mgsh/test_lexer_arith_exp_ctest.c
    test/mgsh/test_lexer_heredoc_ctest.c
    test/mgsh/test_tokenizer_ctest.c
    test/mgsh/test_ast_ctest.c
    test/mgsh/test_expander_ctest.c
)

foreach(test_src ${MGSH_TEST_SOURCES})
    get_filename_component(test_name ${test_src} NAME_WE)

    add_executable(${test_name} ${test_src} $<TARGET_OBJECTS:ctest_obj>)
    # Let's define an 'IN_CTEST' variable just in case
    # one of our test cases needs to #ifdef around normal
    # behavior.    
    target_compile_definitions(${test_name} PRIVATE IN_CTEST)
    target_include_directories(${test_name} PRIVATE
        src
        ${CMAKE_CURRENT_SOURCE_DIR}/test/ctest
    )
    target_link_libraries(${test_name} PRIVATE mgsh_lib)

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
