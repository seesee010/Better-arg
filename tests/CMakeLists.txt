# Every tests/unit/test_*.c becomes one executable and one CTest test.
#
# Tests are compiled with ARG_EXIT_ON_ERROR=0 so that parse errors are
# returned instead of ending the test process.

file(GLOB ARG_UNIT_TESTS CONFIGURE_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/unit/test_*.c)

foreach(src IN LISTS ARG_UNIT_TESTS)
    get_filename_component(name ${src} NAME_WE)
    add_executable(${name} ${src})
    target_link_libraries(${name} PRIVATE arg)
    # White-box tests may include private headers.
    target_include_directories(${name} PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/unit ${PROJECT_SOURCE_DIR}/src)
    target_compile_definitions(${name} PRIVATE ARG_EXIT_ON_ERROR=0)
    arg_target_warnings(${name})
    add_test(NAME ${name} COMMAND ${name})
    set_tests_properties(${name} PROPERTIES LABELS unit TIMEOUT 60)
endforeach()

# End-to-end tests run the example programs.
if(ARG_BUILD_EXAMPLES)
    add_subdirectory(e2e)
endif()
