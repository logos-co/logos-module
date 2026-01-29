# Command-line binary for logos-module
add_executable(lm main.cpp)

# Link against the logos_module library
target_link_libraries(lm PRIVATE logos_module Qt6::Core)

# Set output directory
set_target_properties(lm PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin
)

# Install the binary
install(TARGETS lm
    RUNTIME DESTINATION bin
)
