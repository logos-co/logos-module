# Logos Module Tests

add_executable(logos_module_tests
    test_metadata.cpp
    test_module_directory.cpp
    test_introspection.cpp
    test_cli.cpp
    test_instance_persistence.cpp
)

# Link with appropriate GTest targets (handles both find_package and FetchContent)
if(TARGET GTest::gtest)
    target_link_libraries(logos_module_tests PRIVATE
        logos_module
        Qt6::Core
        GTest::gtest
        GTest::gtest_main
    )
else()
    target_link_libraries(logos_module_tests PRIVATE
        logos_module
        Qt6::Core
        gtest
        gtest_main
    )
endif()

# The fixture directories, by absolute source path. Not searched for at
# runtime: a fixture that cannot be found must fail the run, and a test that
# skips instead renders as a pass.
target_compile_definitions(logos_module_tests PRIVATE
    LOGOS_MODULE_TEST_EXAMPLES="${CMAKE_CURRENT_SOURCE_DIR}/examples")

# Discover tests
gtest_discover_tests(logos_module_tests)
