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

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

project(slwoggy VERSION 1.0.0 LANGUAGES CXX)

# Set default build type to Release
if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "Choose the type of build." FORCE)
    set_property(CACHE CMAKE_BUILD_TYPE PROPERTY STRINGS "Debug" "Release" "MemCheck" "Profile")
endif()

# Options
option(SLWOGGY_BUILD_EXAMPLES "Build example applications" OFF)
option(SLWOGGY_BUILD_TESTS "Build tests" OFF)
option(SLWOGGY_BUILD_AMALGAMATION "Build amalgamation header" OFF)
option(SLWOGGY_RELIABLE_DELIVERY "Enable reliable delivery mode" ON)

# Metrics options (OFF by default for Release builds)
option(SLWOGGY_METRICS_BUFFER_POOL "Enable buffer pool metrics" OFF)
option(SLWOGGY_METRICS_DISPATCHER "Enable dispatcher metrics" OFF)
option(SLWOGGY_METRICS_STRUCTURED "Enable structured logging metrics" OFF)
option(SLWOGGY_METRICS_MSG_RATE "Enable message rate tracking" OFF)
option(SLWOGGY_METRICS_ROTATION "Enable log rotation metrics" OFF)
option(SLWOGGY_METRICS_COMPRESSION "Enable compression metrics" OFF)
option(SLWOGGY_METRICS_ALL "Enable all metrics" OFF)

# Automatically enable examples and tests for Debug builds
if(CMAKE_BUILD_TYPE STREQUAL "Debug" OR CMAKE_BUILD_TYPE STREQUAL "MemCheck")
    set(SLWOGGY_BUILD_EXAMPLES ON CACHE BOOL "Build example applications" FORCE)
    set(SLWOGGY_BUILD_TESTS ON CACHE BOOL "Build tests" FORCE)
    set(SLWOGGY_METRICS_ALL ON CACHE BOOL "Enable all metrics" FORCE)
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
message(STATUS "Build examples: ${SLWOGGY_BUILD_EXAMPLES}")
message(STATUS "Build tests: ${SLWOGGY_BUILD_TESTS}")

# FetchContent for dependencies
include(FetchContent)

# fmt - header-only mode
FetchContent_Declare(
    fmt
    GIT_REPOSITORY https://github.com/fmtlib/fmt.git
    GIT_TAG 10.1.1
    GIT_SHALLOW TRUE
)

# taocpp-json - header-only
FetchContent_Declare(
    taocpp_json
    GIT_REPOSITORY https://github.com/taocpp/json.git
    GIT_TAG 1.0.0-beta.14
    GIT_SHALLOW TRUE
)

# Catch2 - only needed for tests
if(SLWOGGY_BUILD_TESTS)
    FetchContent_Declare(
        Catch2
        GIT_REPOSITORY https://github.com/catchorg/Catch2.git
        GIT_TAG v3.4.0
        GIT_SHALLOW TRUE
    )
endif()

# Set policy for FetchContent_Populate deprecation
if(POLICY CMP0169)
    cmake_policy(SET CMP0169 OLD)
endif()

# For header-only usage, fetch but don't build
FetchContent_GetProperties(fmt)
if(NOT fmt_POPULATED)
    FetchContent_Populate(fmt)
endif()

FetchContent_GetProperties(taocpp_json)
if(NOT taocpp_json_POPULATED)
    FetchContent_Populate(taocpp_json)
endif()

# Create header-only interface targets
if(NOT TARGET fmt::fmt)
    add_library(fmt_header_only INTERFACE)
    target_include_directories(fmt_header_only INTERFACE ${fmt_SOURCE_DIR}/include)
    target_compile_definitions(fmt_header_only INTERFACE FMT_HEADER_ONLY)
    add_library(fmt::fmt ALIAS fmt_header_only)
endif()

if(NOT TARGET taocppjson)
    add_library(taocppjson INTERFACE)
    target_include_directories(taocppjson INTERFACE ${taocpp_json_SOURCE_DIR}/include)
endif()

if(SLWOGGY_BUILD_TESTS)
    # Set C++ standard before fetching Catch2 so it compiles with C++20
    
    # Catch2 needs full processing
    FetchContent_MakeAvailable(Catch2)
    list(APPEND CMAKE_MODULE_PATH ${catch2_SOURCE_DIR}/extras)
endif()

# Create the main slwoggy interface library
add_library(slwoggy INTERFACE)
add_library(slwoggy::slwoggy ALIAS slwoggy)

# Set include directories (build interface only - no installation support)
target_include_directories(slwoggy INTERFACE
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
)

# Propagate include directories from dependencies
target_link_libraries(slwoggy INTERFACE 
    $<BUILD_INTERFACE:fmt::fmt>
    $<BUILD_INTERFACE:taocppjson>
)

# Set C++ standard requirement
target_compile_features(slwoggy INTERFACE cxx_std_20)

# Add warning flags
target_compile_options(slwoggy INTERFACE
    $<$<COMPILE_LANGUAGE:CXX>:-Wall -Wextra>
)

# Platform-specific link libraries
if(UNIX AND NOT APPLE)
    target_link_libraries(slwoggy INTERFACE pthread m dl)
elseif(APPLE)
    # macOS doesn't need special libraries for slwoggy
endif()

# Metrics configuration
set(_SLWOGGY_COMPILE_DEFINITIONS "")

if(SLWOGGY_METRICS_ALL)
    list(APPEND _SLWOGGY_COMPILE_DEFINITIONS
        LOG_COLLECT_BUFFER_POOL_METRICS
        LOG_COLLECT_DISPATCHER_METRICS
        LOG_COLLECT_STRUCTURED_METRICS
        LOG_COLLECT_DISPATCHER_MSG_RATE
        LOG_COLLECT_ROTATION_METRICS
        LOG_COLLECT_COMPRESSION_METRICS
    )
else()
    if(SLWOGGY_METRICS_BUFFER_POOL)
        list(APPEND _SLWOGGY_COMPILE_DEFINITIONS LOG_COLLECT_BUFFER_POOL_METRICS)
    endif()
    if(SLWOGGY_METRICS_DISPATCHER)
        list(APPEND _SLWOGGY_COMPILE_DEFINITIONS LOG_COLLECT_DISPATCHER_METRICS)
    endif()
    if(SLWOGGY_METRICS_STRUCTURED)
        list(APPEND _SLWOGGY_COMPILE_DEFINITIONS LOG_COLLECT_STRUCTURED_METRICS)
    endif()
    if(SLWOGGY_METRICS_MSG_RATE)
        list(APPEND _SLWOGGY_COMPILE_DEFINITIONS LOG_COLLECT_DISPATCHER_MSG_RATE)
    endif()
    if (SLWOGGY_METRICS_ROTATION)
        list(APPEND _SLWOGGY_COMPILE_DEFINITIONS LOG_COLLECT_ROTATION_METRICS)
    endif()
    if (SLWOGGY_METRICS_COMPRESSION)
        list(APPEND _SLWOGGY_COMPILE_DEFINITIONS LOG_COLLECT_COMPRESSION_METRICS)
    endif()
endif()


# Apply compile definitions
if(_SLWOGGY_COMPILE_DEFINITIONS)
    target_compile_definitions(slwoggy INTERFACE ${_SLWOGGY_COMPILE_DEFINITIONS})
endif()

# Add SLWOGGY_RELIABLE_DELIVERY if enabled
if(SLWOGGY_RELIABLE_DELIVERY)
    target_compile_definitions(slwoggy INTERFACE SLWOGGY_RELIABLE_DELIVERY)
endif()

# Version definition
target_compile_definitions(slwoggy INTERFACE SLWOGGY_VERSION_STRING="${GIT_VERSION}")

# Enable additional debug features for MemCheck builds
if(CMAKE_BUILD_TYPE STREQUAL "MemCheck")
    target_compile_definitions(slwoggy INTERFACE
        _GLIBCXX_DEBUG
        _GLIBCXX_DEBUG_PEDANTIC
    )
    message(STATUS "MemCheck build enabled with AddressSanitizer and UndefinedBehaviorSanitizer")
    message(STATUS "Run with: ASAN_OPTIONS=detect_leaks=1:check_initialization_order=1:strict_init_order=1")
endif()

# Generate compile_commands.json
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# Build examples if requested
if(SLWOGGY_BUILD_EXAMPLES)
    add_subdirectory(src)
endif()

# Build tests if requested
if(SLWOGGY_BUILD_TESTS)
    enable_testing()
    include(CTest)
    include(Catch)
    add_subdirectory(tests)
endif()

# Amalgamation target
if(SLWOGGY_BUILD_AMALGAMATION)
    add_custom_target(amalgamation ALL
        COMMAND ${CMAKE_CURRENT_SOURCE_DIR}/create-amalgamation.sh
        WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
        COMMENT "Creating single-header amalgamation"
        VERBATIM
    )
else()
    add_custom_target(amalgamation
        COMMAND ${CMAKE_CURRENT_SOURCE_DIR}/create-amalgamation.sh
        WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
        COMMENT "Creating single-header amalgamation"
        VERBATIM
    )
endif()

# Print summary
message(STATUS "")
message(STATUS "slwoggy configuration summary:")
message(STATUS "  Version: ${GIT_VERSION}")
message(STATUS "  Build type: ${CMAKE_BUILD_TYPE}")
message(STATUS "  Examples: ${SLWOGGY_BUILD_EXAMPLES}")
message(STATUS "  Tests: ${SLWOGGY_BUILD_TESTS}")
message(STATUS "  Reliable delivery: ${SLWOGGY_RELIABLE_DELIVERY}")
message(STATUS "  Metrics: ${_SLWOGGY_COMPILE_DEFINITIONS}")
message(STATUS "")