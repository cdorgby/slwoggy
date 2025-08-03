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

# Compiler flags
if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    add_compile_options(-Wno-interference-size)
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
target_include_directories(${PROJECT_NAME} PRIVATE ${CMAKE_SOURCE_DIR}/include)

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

# Set output directories
set_target_properties(${PROJECT_NAME} PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
)

# Platform-specific settings
if(WIN32)
    # Windows-specific settings
    if(MINGW)
        target_link_libraries(${PROJECT_NAME} -static-libgcc -static-libstdc++)
    endif()
elseif(APPLE)
    # macOS-specific settings - no special frameworks needed for this test app
elseif(UNIX)
    # Linux-specific settings
    target_link_libraries(${PROJECT_NAME} m pthread dl)
endif()

# Enable testing support
enable_testing()

# Include FetchContent module
include(FetchContent)

# Add Catch2
FetchContent_Declare(
    Catch2
    GIT_REPOSITORY https://github.com/catchorg/Catch2.git
    GIT_TAG v3.5.0
)

# Only fetch and build Catch2 when tests are being built
FetchContent_GetProperties(Catch2)
if(NOT catch2_POPULATED)
    FetchContent_Populate(Catch2)
    # Add Catch2 with EXCLUDE_FROM_ALL so it's not built by default
    add_subdirectory(${catch2_SOURCE_DIR} ${catch2_BINARY_DIR} EXCLUDE_FROM_ALL)
endif()

# Include Catch2's CMake utilities
list(APPEND CMAKE_MODULE_PATH ${catch2_SOURCE_DIR}/extras)
include(CTest)
include(Catch)

# Add tests subdirectory
add_subdirectory(tests)

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