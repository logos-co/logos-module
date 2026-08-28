cmake_minimum_required(VERSION 3.16)
project(logos_module VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)

# Find Qt6
find_package(Qt6 REQUIRED COMPONENTS Core)

# lgx: logos-package's C ABI, which owns every format and integrity rule this
# library reports. Required rather than optional — a build that cannot reach it
# would have to answer "unknown" to every installed-directory check, and a
# check that silently degrades is worse than no check at all.
set(LOGOS_PACKAGE_ROOT "" CACHE PATH "Root of the lgx library (include/ and lib/)")
find_path(LGX_INCLUDE_DIR lgx.h HINTS ${LOGOS_PACKAGE_ROOT}/include $ENV{LOGOS_PACKAGE_ROOT}/include)
find_library(LGX_LIBRARY NAMES lgx liblgx HINTS ${LOGOS_PACKAGE_ROOT}/lib $ENV{LOGOS_PACKAGE_ROOT}/lib)
if(NOT LGX_INCLUDE_DIR OR NOT LGX_LIBRARY)
    message(FATAL_ERROR
        "lgx not found. Pass -DLOGOS_PACKAGE_ROOT=<logos-package lib output>, "
        "or build through nix, which supplies it. "
        "(include: ${LGX_INCLUDE_DIR}, library: ${LGX_LIBRARY})")
endif()

# liblgx's install name is @rpath-relative, so an executable that links it finds
# it only if the directory is on its rpath. Set before any target is created,
# and inherited by cmd/ and tests/.
if(UNIX)
    get_filename_component(LGX_LIBRARY_DIR "${LGX_LIBRARY}" DIRECTORY)
    list(APPEND CMAKE_BUILD_RPATH "${LGX_LIBRARY_DIR}")
    list(APPEND CMAKE_INSTALL_RPATH "${LGX_LIBRARY_DIR}")
endif()

# Define source files
set(MODULE_LIB_SOURCES
    src/module_metadata.cpp
    src/module_directory.cpp
    src/installed_checks.cpp
    src/logos_module.cpp
    src/instance_persistence.cpp
)

set(MODULE_LIB_HEADERS
    src/module_metadata.h
    src/module_directory.h
    src/logos_module.h
    src/logos_provider_plugin.h
    src/module_lib.h
    src/interface.h
    src/instance_persistence.h
    src/win_dll_search.h
)

# Create the static library
add_library(logos_module STATIC ${MODULE_LIB_SOURCES} ${MODULE_LIB_HEADERS})

# Link Qt6
target_link_libraries(logos_module PUBLIC Qt6::Core)

# PRIVATE, and confined to installed_checks.cpp: a consumer that only opens a
# ModuleDirectory pulls no object referencing lgx and needs no -llgx.
target_link_libraries(logos_module PRIVATE ${LGX_LIBRARY})
target_include_directories(logos_module PRIVATE ${LGX_INCLUDE_DIR})

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
