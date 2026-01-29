# Logos Module Tests

add_executable(logos_module_tests
    test_metadata.cpp
    test_introspection.cpp
    test_cli.cpp
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

# Discover tests
gtest_discover_tests(logos_module_tests)
