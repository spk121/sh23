cmake_minimum_required(VERSION 3.16)
project(mgsh LANGUAGES C)

if(MSVC)
    add_compile_options(/std:clatest /W4 /Od /Zi)
else()
    add_compile_options(-Wall -Wextra -g3 -O0)
endif()

add_executable(mgsh
    src/alias.c
    src/alias.h
    src/alias_store.c
    src/alias_store.h
    src/alias_array.c
    src/alias_array.h
    src/logging.c
    src/logging.h
    src/string.c
    src/string.h

    src/main.c
)
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
    test/mgsh/test_alias_ctest.c
)

foreach(test_src ${CTEST_SOURCES})
    get_filename_component(test_name ${test_src} NAME_WE)  # e.g. test_substitute_ctest
    add_executable(${test_name} ${test_src}
        src/alias.c
        src/alias.h
        src/alias_store.c
        src/alias_store.h
        src/alias_array.c
        src/alias_array.h
        src/logging.c
        src/logging.h
        src/string.c
        src/string.h
        $<TARGET_OBJECTS:ctest_obj>
    )
    target_compile_definitions(${test_name} PRIVATE CTEST)
    target_link_libraries(${test_name} PRIVATE vc)
    target_include_directories(${test_name} PRIVATE
        src
        ${CMAKE_CURRENT_SOURCE_DIR}/test/ctest
    )
    set_target_properties(${test_name} PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/test
    )

    # Register with CTest (TAP compatible)
    add_test(NAME ${test_name}
        COMMAND ${test_name}
        WORKING_DIRECTORY ${CMAKE_BINARY_DIR}/test
    )

    set_tests_properties(${test_name} PROPERTIES
        FAIL_REGULAR_EXPRESSION "not ok|\\b(Bail out!|Bailout!)\\b"
        PASS_REGULAR_EXPRESSION "ok"
    )

endforeach()

