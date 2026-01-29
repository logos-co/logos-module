cmake_minimum_required(VERSION 3.16)
project(logos_module VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)

# Find Qt6
find_package(Qt6 REQUIRED COMPONENTS Core)

# Define source files
set(MODULE_LIB_SOURCES
    src/module_metadata.cpp
    src/module_loader.cpp
    src/module_introspection.cpp
)

set(MODULE_LIB_HEADERS
    src/module_metadata.h
    src/module_loader.h
    src/module_introspection.h
    src/module_lib.h
)

# Create the static library
add_library(logos_module STATIC ${MODULE_LIB_SOURCES} ${MODULE_LIB_HEADERS})

# Link Qt6
target_link_libraries(logos_module PUBLIC Qt6::Core)

# Include directories for building
target_include_directories(logos_module PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/src>
    $<INSTALL_INTERFACE:include/module_lib>
)

# Install the library
install(TARGETS logos_module
    ARCHIVE DESTINATION lib
    LIBRARY DESTINATION lib
)

# Install headers to include/module_lib/
install(FILES ${MODULE_LIB_HEADERS}
    DESTINATION include/module_lib
)

# Add command-line binary
add_subdirectory(cmd)

# Testing (optional)
option(LOGOS_MODULE_BUILD_TESTS "Build tests" OFF)
if(LOGOS_MODULE_BUILD_TESTS)
    # Try to find Google Test first (for Nix and system installations)
    # Fall back to FetchContent if not found
    find_package(GTest QUIET)
    if(NOT GTest_FOUND)
        message(STATUS "GTest not found, fetching from GitHub")
        include(FetchContent)
        FetchContent_Declare(
            googletest
            GIT_REPOSITORY https://github.com/google/googletest.git
            GIT_TAG v1.14.0
        )
        # Prevent overriding the parent project's compiler/linker settings on Windows
        set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
        FetchContent_MakeAvailable(googletest)
    endif()
    
    enable_testing()
    include(GoogleTest)
    add_subdirectory(tests)
endif()
