# Define test sources
set(TEST_SOURCES
    main.cpp
    test_log.cpp
    test_log_structured.cpp
    test_log_statistics.cpp
    test_log_level_control.cpp
    test_log_site_control.cpp
    test_log_filters.cpp
    test_rotation.cpp
    test_sink_filters.cpp
)

# Function to set per-file compile definitions for tests
function(set_test_source_file_compile_definitions TARGET SOURCES)
    foreach(SOURCE ${SOURCES})
        # Get relative path from project root
        file(RELATIVE_PATH REL_PATH ${CMAKE_SOURCE_DIR} ${CMAKE_CURRENT_SOURCE_DIR}/${SOURCE})
        # Set compile definition for this specific source file
        set_source_files_properties(${SOURCE} PROPERTIES
            COMPILE_DEFINITIONS "SOURCE_FILE_NAME=\"${REL_PATH}\""
        )
    endforeach()
endfunction()

# Function to add metrics definitions to test targets
function(add_metrics_definitions TARGET)
    if(CMAKE_BUILD_TYPE STREQUAL "Debug" OR CMAKE_BUILD_TYPE STREQUAL "MemCheck" OR CMAKE_BUILD_TYPE STREQUAL "Profile")
        target_compile_definitions(${TARGET} PRIVATE
            LOG_COLLECT_BUFFER_POOL_METRICS
            LOG_COLLECT_DISPATCHER_METRICS
            LOG_COLLECT_STRUCTURED_METRICS
            LOG_COLLECT_DISPATCHER_MSG_RATE
            LOG_COLLECT_ROTATION_METRICS
            LOG_COLLECT_COMPRESSION_METRICS
        )
    endif()
    # Apply LOG_RELIABLE_DELIVERY setting from parent
    if(LOG_RELIABLE_DELIVERY)
        target_compile_definitions(${TARGET} PRIVATE
            LOG_RELIABLE_DELIVERY
        )
    endif()
endfunction()

# Note: all_tests removed - run individual test executables instead
# Each test file has its own executable for better isolation

# Individual test executables (also excluded from default build)
set(TEST_LOG_SOURCES main.cpp test_log.cpp)
add_executable(test_log ${TEST_LOG_SOURCES})
set_target_properties(test_log PROPERTIES EXCLUDE_FROM_ALL TRUE)
set_test_source_file_compile_definitions(test_log "${TEST_LOG_SOURCES}")
target_link_libraries(test_log PRIVATE Catch2::Catch2WithMain slwoggy::slwoggy)
add_metrics_definitions(test_log)
catch_discover_tests(test_log)

# Structured logging tests
set(TEST_LOG_STRUCTURED_SOURCES main.cpp test_log_structured.cpp)
add_executable(test_log_structured ${TEST_LOG_STRUCTURED_SOURCES})
set_target_properties(test_log_structured PROPERTIES EXCLUDE_FROM_ALL TRUE)
set_test_source_file_compile_definitions(test_log_structured "${TEST_LOG_STRUCTURED_SOURCES}")
target_link_libraries(test_log_structured PRIVATE Catch2::Catch2WithMain slwoggy::slwoggy)
add_metrics_definitions(test_log_structured)
catch_discover_tests(test_log_structured)

# Statistics tests - enable metrics collection for this test
set(TEST_LOG_STATISTICS_SOURCES main.cpp test_log_statistics.cpp)
add_executable(test_log_statistics ${TEST_LOG_STATISTICS_SOURCES})
set_target_properties(test_log_statistics PROPERTIES EXCLUDE_FROM_ALL TRUE)
set_test_source_file_compile_definitions(test_log_statistics "${TEST_LOG_STATISTICS_SOURCES}")
target_compile_definitions(test_log_statistics PRIVATE 
    LOG_COLLECT_BUFFER_POOL_METRICS
    LOG_COLLECT_DISPATCHER_METRICS
    LOG_COLLECT_STRUCTURED_METRICS
    LOG_COLLECT_ROTATION_METRICS
    LOG_COLLECT_COMPRESSION_METRICS
)
target_link_libraries(test_log_statistics PRIVATE Catch2::Catch2WithMain slwoggy::slwoggy)
add_metrics_definitions(test_log_statistics)
catch_discover_tests(test_log_statistics)

# Level control tests
set(TEST_LOG_LEVEL_CONTROL_SOURCES main.cpp test_log_level_control.cpp)
add_executable(test_log_level_control ${TEST_LOG_LEVEL_CONTROL_SOURCES})
set_target_properties(test_log_level_control PROPERTIES EXCLUDE_FROM_ALL TRUE)
set_test_source_file_compile_definitions(test_log_level_control "${TEST_LOG_LEVEL_CONTROL_SOURCES}")
target_link_libraries(test_log_level_control PRIVATE Catch2::Catch2WithMain slwoggy::slwoggy)
add_metrics_definitions(test_log_level_control)
catch_discover_tests(test_log_level_control)

# Site control tests
set(TEST_LOG_SITE_CONTROL_SOURCES main.cpp test_log_site_control.cpp)
add_executable(test_log_site_control ${TEST_LOG_SITE_CONTROL_SOURCES})
set_target_properties(test_log_site_control PROPERTIES EXCLUDE_FROM_ALL TRUE)
set_test_source_file_compile_definitions(test_log_site_control "${TEST_LOG_SITE_CONTROL_SOURCES}")
target_link_libraries(test_log_site_control PRIVATE Catch2::Catch2WithMain slwoggy::slwoggy)
add_metrics_definitions(test_log_site_control)
catch_discover_tests(test_log_site_control)

# Filter tests
set(TEST_LOG_FILTERS_SOURCES main.cpp test_log_filters.cpp)
add_executable(test_log_filters ${TEST_LOG_FILTERS_SOURCES})
set_target_properties(test_log_filters PROPERTIES EXCLUDE_FROM_ALL TRUE)
set_test_source_file_compile_definitions(test_log_filters "${TEST_LOG_FILTERS_SOURCES}")
target_link_libraries(test_log_filters PRIVATE Catch2::Catch2WithMain slwoggy::slwoggy)
add_metrics_definitions(test_log_filters)
catch_discover_tests(test_log_filters)

# Rotation tests
set(TEST_ROTATION_SOURCES main.cpp test_rotation.cpp)
add_executable(test_rotation ${TEST_ROTATION_SOURCES})
set_target_properties(test_rotation PROPERTIES EXCLUDE_FROM_ALL TRUE)
set_test_source_file_compile_definitions(test_rotation "${TEST_ROTATION_SOURCES}")
target_link_libraries(test_rotation PRIVATE Catch2::Catch2WithMain slwoggy::slwoggy)
add_metrics_definitions(test_rotation)
catch_discover_tests(test_rotation)

# Sink filter tests
set(TEST_SINK_FILTERS_SOURCES main.cpp test_sink_filters.cpp)
add_executable(test_sink_filters ${TEST_SINK_FILTERS_SOURCES})
set_target_properties(test_sink_filters PROPERTIES EXCLUDE_FROM_ALL TRUE)
set_test_source_file_compile_definitions(test_sink_filters "${TEST_SINK_FILTERS_SOURCES}")
target_link_libraries(test_sink_filters PRIVATE Catch2::Catch2WithMain slwoggy::slwoggy)
add_metrics_definitions(test_sink_filters)
catch_discover_tests(test_sink_filters)

# Custom target to build all tests
add_custom_target(tests
    DEPENDS test_log test_log_structured test_log_statistics test_log_level_control test_log_site_control test_log_filters test_rotation test_sink_filters
    COMMENT "Building all test executables"
)

