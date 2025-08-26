# CMakeLists.txt for slwoggy example applications

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

# Helper function to add example executable
function(add_slwoggy_example NAME SOURCE)
    add_executable(${NAME} ${SOURCE})
    target_link_libraries(${NAME} PRIVATE slwoggy::slwoggy)
    set_source_file_compile_definitions(${NAME} "${SOURCE}")
    
    # Set output directory
    set_target_properties(${NAME} PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
    )
    
    # Platform-specific settings for examples
    if(WIN32 AND MINGW)
        target_link_libraries(${NAME} PRIVATE -static-libgcc -static-libstdc++)
    endif()
endfunction()

# Main slwoggy demo
add_slwoggy_example(slwoggy_demo main.cpp)

# Simple example
add_slwoggy_example(simple simple.cpp)

# Rotation demo
add_slwoggy_example(rotation_demo rotation_demo.cpp)

# Log modules demo
add_slwoggy_example(log_mods log_mods.cpp)

# Sink filter demo
add_slwoggy_example(sink_filter_demo sink_filter_demo.cpp)

# Module filter demo
add_slwoggy_example(module_filter_demo module_filter_demo.cpp)

# 
add_slwoggy_example(custom_prints custom_prints.cpp)