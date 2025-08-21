cmake_minimum_required(VERSION 3.11)
project(slwoggy_integration_test)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Method 1: add_subdirectory for local testing
# In real usage, you would use FetchContent with a Git repository
add_subdirectory(.. slwoggy)

# Alternatively, for FetchContent from GitHub:
# include(FetchContent)
# FetchContent_Declare(
#     slwoggy
#     GIT_REPOSITORY https://github.com/your-org/slwoggy.git
#     GIT_TAG main
# )
# FetchContent_MakeAvailable(slwoggy)

# Create test executable
add_executable(integration_test main.cpp)

# Link against slwoggy
target_link_libraries(integration_test PRIVATE slwoggy::slwoggy)

# Test that we can use the library
add_test(NAME integration_test COMMAND integration_test)
enable_testing()