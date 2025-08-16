cmake_minimum_required(VERSION 3.11)

# Get version from git tags
execute_process(
    COMMAND git describe --tags --always --dirty
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
    OUTPUT_VARIABLE GIT_VERSION
    OUTPUT_STRIP_TRAILING_WHITESPACE
    ERROR_QUIET
)
if(NOT GIT_VERSION)
    set(GIT_VERSION "dev")
endif()

project(slwoggy)

# Make git version available
add_compile_definitions(SLWOGGY_VERSION_STRING="${GIT_VERSION}")

# Set default build type if not specified
if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Debug CACHE STRING "Choose the type of build." FORCE)
    set_property(CACHE CMAKE_BUILD_TYPE PROPERTY STRINGS "Debug" "Release" "MemCheck" "Profile")
endif()

# Custom MemCheck build type
set(CMAKE_CXX_FLAGS_MEMCHECK "${CMAKE_CXX_FLAGS_DEBUG} -fsanitize=address,undefined -fno-omit-frame-pointer -fno-optimize-sibling-calls" CACHE STRING 
    "Flags used by the C++ compiler during MemCheck builds." FORCE)
set(CMAKE_C_FLAGS_MEMCHECK "${CMAKE_C_FLAGS_DEBUG} -fsanitize=address,undefined -fno-omit-frame-pointer -fno-optimize-sibling-calls" CACHE STRING 
    "Flags used by the C compiler during MemCheck builds." FORCE)
set(CMAKE_EXE_LINKER_FLAGS_MEMCHECK "${CMAKE_EXE_LINKER_FLAGS_DEBUG} -fsanitize=address,undefined" CACHE STRING 
    "Flags used for linking binaries during MemCheck builds." FORCE)
set(CMAKE_SHARED_LINKER_FLAGS_MEMCHECK "${CMAKE_SHARED_LINKER_FLAGS_DEBUG} -fsanitize=address,undefined" CACHE STRING 
    "Flags used by the shared libraries linker during MemCheck builds." FORCE)

# Custom Profile build type
set(CMAKE_CXX_FLAGS_PROFILE "-O2 -g -fno-omit-frame-pointer -DNDEBUG" CACHE STRING 
    "Flags used by the C++ compiler during Profile builds." FORCE)
set(CMAKE_C_FLAGS_PROFILE "-O2 -g -fno-omit-frame-pointer -DNDEBUG" CACHE STRING 
    "Flags used by the C compiler during Profile builds." FORCE)
set(CMAKE_EXE_LINKER_FLAGS_PROFILE "" CACHE STRING 
    "Flags used for linking binaries during Profile builds." FORCE)
set(CMAKE_SHARED_LINKER_FLAGS_PROFILE "" CACHE STRING 
    "Flags used by the shared libraries linker during Profile builds." FORCE)

# Mark advanced variables
mark_as_advanced(
    CMAKE_CXX_FLAGS_MEMCHECK
    CMAKE_C_FLAGS_MEMCHECK
    CMAKE_EXE_LINKER_FLAGS_MEMCHECK
    CMAKE_SHARED_LINKER_FLAGS_MEMCHECK
    CMAKE_CXX_FLAGS_PROFILE
    CMAKE_C_FLAGS_PROFILE
    CMAKE_EXE_LINKER_FLAGS_PROFILE
    CMAKE_SHARED_LINKER_FLAGS_PROFILE)

# Display current build type
message(STATUS "Build type: ${CMAKE_BUILD_TYPE}")

# Enable additional debug features for MemCheck builds
if(CMAKE_BUILD_TYPE STREQUAL "MemCheck")
    add_compile_definitions(
        _GLIBCXX_DEBUG  # Enable libstdc++ debug mode
        _GLIBCXX_DEBUG_PEDANTIC  # Enable pedantic debug mode
    )
    # Set environment variable for ASan options at runtime
    set(CMAKE_RUNTIME_OUTPUT_DIRECTORY_MEMCHECK ${CMAKE_BINARY_DIR}/bin)
    message(STATUS "MemCheck build enabled with AddressSanitizer and UndefinedBehaviorSanitizer")
    message(STATUS "Run with: ASAN_OPTIONS=detect_leaks=1:check_initialization_order=1:strict_init_order=1")
endif()

# Enable profiling features for Profile builds
if(CMAKE_BUILD_TYPE STREQUAL "Profile")
    message(STATUS "Profile build enabled with optimization level 2 and debug symbols")
    message(STATUS "Frame pointers preserved for accurate profiling")
endif()

# Generate compile_commands.json
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# C++ standard
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Options
option(LOG_RELIABLE_DELIVERY "Enable blocking behavior when buffer pool exhausted (reliable delivery)" ON)

# Compiler flags
if(CMAKE_CXX_COMPILER_ID MATCHES "GNU")
    # GCC-specific flags
    add_compile_options(-Wno-interference-size)
elseif(CMAKE_CXX_COMPILER_ID MATCHES "Clang")
    # Clang-specific flags (if needed)
endif()

# Function to set per-file compile definitions
function(set_source_file_compile_definitions TARGET SOURCES)
    foreach(SOURCE ${SOURCES})
        # Get relative path from project root
        file(RELATIVE_PATH REL_PATH ${CMAKE_SOURCE_DIR} ${CMAKE_CURRENT_SOURCE_DIR}/${SOURCE})
        # Set compile definition for this specific source file
        set_source_files_properties(${SOURCE} PROPERTIES
            COMPILE_DEFINITIONS "SOURCE_FILE_NAME=\"${REL_PATH}\""
        )
    endforeach()
endfunction()

# Add source files
set(SOURCES
    src/main.cpp
)

# Add our executable
add_executable(${PROJECT_NAME} ${SOURCES})

# Add include directories
target_include_directories(${PROJECT_NAME} PRIVATE 
    ${CMAKE_SOURCE_DIR}/include
)

# Link with taocpp/json and fmt
target_link_libraries(${PROJECT_NAME} PRIVATE taocppjson fmt::fmt)

# Set per-file compile definitions
set_source_file_compile_definitions(${PROJECT_NAME} "${SOURCES}")

# Enable metrics collection for Debug, MemCheck, and Profile builds
if(CMAKE_BUILD_TYPE STREQUAL "Debug" OR CMAKE_BUILD_TYPE STREQUAL "MemCheck" OR CMAKE_BUILD_TYPE STREQUAL "Profile")
    target_compile_definitions(${PROJECT_NAME} PRIVATE
        LOG_COLLECT_BUFFER_POOL_METRICS
        LOG_COLLECT_DISPATCHER_METRICS
        LOG_COLLECT_STRUCTURED_METRICS
        LOG_COLLECT_DISPATCHER_MSG_RATE
    )
endif()

# Reliable delivery option
if(LOG_RELIABLE_DELIVERY)
    target_compile_definitions(${PROJECT_NAME} PRIVATE
        LOG_RELIABLE_DELIVERY
    )
    message(STATUS "Reliable delivery: ENABLED (blocking mode)")
else()
    message(STATUS "Reliable delivery: DISABLED (best-effort mode)")
endif()

# Set output directories
set_target_properties(${PROJECT_NAME} PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
)

# Platform-specific settings
if(WIN32)
    # Windows-specific settings
    if(MINGW)
        target_link_libraries(${PROJECT_NAME} PRIVATE -static-libgcc -static-libstdc++)
    endif()
elseif(APPLE)
    # macOS-specific settings - no special frameworks needed for this test app
elseif(UNIX)
    # Linux-specific settings
    target_link_libraries(${PROJECT_NAME} PRIVATE m pthread dl)
endif()

# Enable testing support
enable_testing()

# Check if git submodules are initialized
if(NOT EXISTS "${CMAKE_SOURCE_DIR}/third_party/fmt/CMakeLists.txt")
    message(FATAL_ERROR "Git submodules not initialized. Please run: git submodule update --init --recursive")
endif()

# Configure third-party dependencies
set(BUILD_TESTING OFF CACHE BOOL "" FORCE)  # Don't build tests for dependencies

# fmt configuration
set(FMT_DOC OFF CACHE BOOL "" FORCE)
set(FMT_TEST OFF CACHE BOOL "" FORCE)
set(FMT_INSTALL OFF CACHE BOOL "" FORCE)

# Add fmt - only built when something links to it
add_subdirectory(third_party/fmt EXCLUDE_FROM_ALL)

# Add taocpp/json as interface library (header-only)
add_library(taocppjson INTERFACE)
target_include_directories(taocppjson INTERFACE ${CMAKE_SOURCE_DIR}/third_party/taocpp-json/include)

# Add Catch2 - only built when tests are built
add_subdirectory(third_party/Catch2 EXCLUDE_FROM_ALL)

# Include Catch2's CMake utilities for tests
list(APPEND CMAKE_MODULE_PATH ${CMAKE_SOURCE_DIR}/third_party/Catch2/extras)
include(CTest)
include(Catch)

# Add tests subdirectory with EXCLUDE_FROM_ALL
# Tests will only be built when explicitly requested (e.g., make tests)
add_subdirectory(tests EXCLUDE_FROM_ALL)

# Simple installation rule - just install the executable
install(TARGETS ${PROJECT_NAME}
    RUNTIME DESTINATION bin
)

# Amalgamation target
add_custom_target(amalgamation
    COMMAND ${CMAKE_CURRENT_SOURCE_DIR}/create-amalgamation.sh
    WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
    COMMENT "Creating single-header amalgamation"
    VERBATIM
)

# Install amalgamation header if it exists
if(EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/slwoggy.hpp)
    install(FILES slwoggy.hpp DESTINATION include)
endif()