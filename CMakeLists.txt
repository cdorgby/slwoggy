cmake_minimum_required(VERSION 3.11)
project(RaylibHelloWorld)

#set(CMAKE_BUILD_TYPE Release)  # Automatically adds -DNDEBUG
set(CMAKE_BUILD_TYPE Debug)

# Project display name for installers/packages
set(PROJECT_DISPLAY_NAME "Raylib Hello World")

# Generate compile_commands.json
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# C++ standard
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Add raylib
include(FetchContent)
FetchContent_Declare(
    raylib
    GIT_REPOSITORY https://github.com/raysan5/raylib.git
    GIT_TAG 5.5
)
FetchContent_MakeAvailable(raylib)

# Add Lua - always build from source for consistency
FetchContent_Declare(
    lua
    URL https://www.lua.org/ftp/lua-5.4.6.tar.gz
    URL_HASH SHA256=7d5ea1b9cb6aa0b59ca3dde1c6adcb57ef83a1ba8e5432c0ecd06bf439b3ad88
)

# Add nlohmann/json
FetchContent_Declare(
    json
    GIT_REPOSITORY https://github.com/nlohmann/json.git
    GIT_TAG v3.11.3
)

FetchContent_GetProperties(lua)
if(NOT lua_POPULATED)
    FetchContent_Populate(lua)
    
    # Build Lua as a library
    file(GLOB LUA_SOURCES ${lua_SOURCE_DIR}/src/*.c)
    list(REMOVE_ITEM LUA_SOURCES ${lua_SOURCE_DIR}/src/lua.c ${lua_SOURCE_DIR}/src/luac.c)
    
    add_library(lua STATIC ${LUA_SOURCES})
    target_include_directories(lua PUBLIC ${lua_SOURCE_DIR}/src)
    
    # Platform-specific defines
    if(UNIX AND NOT APPLE)
        target_compile_definitions(lua PRIVATE LUA_USE_LINUX)
    elseif(APPLE)
        target_compile_definitions(lua PRIVATE LUA_USE_MACOSX)
    endif()
    
    set(LUA_LIBRARIES lua)
    set(LUA_INCLUDE_DIR ${lua_SOURCE_DIR}/src)
endif()

# Make nlohmann/json available
FetchContent_MakeAvailable(json)

# Function to set per-file compile definitions
function(set_source_file_compile_definitions TARGET SOURCES)
    foreach(SOURCE ${SOURCES})
        # Get relative path from project root
        file(RELATIVE_PATH REL_PATH ${CMAKE_SOURCE_DIR} ${CMAKE_CURRENT_SOURCE_DIR}/${SOURCE})
        # Set compile definition for this specific source file
        set_source_files_properties(${SOURCE} PROPERTIES
            COMPILE_DEFINITIONS "TUBS_SOURCE_DIR=\"${REL_PATH}\""
        )
    endforeach()
endfunction()

# Add source files
set(SOURCES
    src/main.cpp
    src/ResourceManager.cpp
    src/ApplicationView.cpp
    src/InputManager.cpp
    src/lua/LuaState.cpp
    src/lua/LuaManager.cpp
    src/lua/ScriptContext.cpp

    src/log.c
)

# Add our executable
add_executable(${PROJECT_NAME} ${SOURCES})

# Set per-file compile definitions
set_source_file_compile_definitions(${PROJECT_NAME} "${SOURCES}")

# Include directories
target_include_directories(${PROJECT_NAME} PRIVATE 
    include
    ${LUA_INCLUDE_DIR}
)

# Link libraries
target_link_libraries(${PROJECT_NAME} 
    raylib
    ${LUA_LIBRARIES}
    nlohmann_json::nlohmann_json
)

# Set output directories
set_target_properties(${PROJECT_NAME} PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
)

# Platform-specific settings
if(WIN32)
    # Windows-specific settings
    set_target_properties(${PROJECT_NAME} PROPERTIES
        SUFFIX ".exe"
    )
    if(MINGW)
        target_link_libraries(${PROJECT_NAME} -static-libgcc -static-libstdc++)
    endif()
elseif(APPLE)
    # macOS-specific settings
    target_link_libraries(${PROJECT_NAME} "-framework IOKit" "-framework Cocoa" "-framework OpenGL")
elseif(UNIX)
    # Linux-specific settings
    target_link_libraries(${PROJECT_NAME} m pthread dl)
endif()

# Installation rules
if(APPLE)
    set_target_properties(${PROJECT_NAME} PROPERTIES
        MACOSX_BUNDLE TRUE
        MACOSX_BUNDLE_GUI_IDENTIFIER "com.example.${PROJECT_NAME}"
        MACOSX_BUNDLE_BUNDLE_NAME "${PROJECT_DISPLAY_NAME}"
        MACOSX_BUNDLE_SHORT_VERSION_STRING "1.0"
        MACOSX_BUNDLE_BUNDLE_VERSION "1.0.0"
    )
    
    # Install bundle
    install(TARGETS ${PROJECT_NAME}
        BUNDLE DESTINATION .
        RUNTIME DESTINATION bin
    )
else()
    # Install executable
    install(TARGETS ${PROJECT_NAME}
        RUNTIME DESTINATION bin
    )
endif()

# CPack configuration
set(CPACK_PACKAGE_NAME "${PROJECT_NAME}")
set(CPACK_PACKAGE_VENDOR "Example Company")
set(CPACK_PACKAGE_DESCRIPTION_SUMMARY "A simple Hello World application using raylib")
set(CPACK_PACKAGE_VERSION_MAJOR "1")
set(CPACK_PACKAGE_VERSION_MINOR "0")
set(CPACK_PACKAGE_VERSION_PATCH "0")
set(CPACK_PACKAGE_CONTACT "your-email@example.com")

# Platform-specific packaging
if(WIN32)
    # Windows installer
    set(CPACK_GENERATOR "NSIS;ZIP")
    set(CPACK_NSIS_DISPLAY_NAME "${PROJECT_DISPLAY_NAME}")
    set(CPACK_NSIS_PACKAGE_NAME "${PROJECT_DISPLAY_NAME}")
    set(CPACK_NSIS_CREATE_ICONS_EXTRA
        "CreateShortCut '$SMPROGRAMS\\\\$STARTMENU_FOLDER\\\\${PROJECT_DISPLAY_NAME}.lnk' '$INSTDIR\\\\bin\\\\${PROJECT_NAME}.exe'"
    )
    set(CPACK_NSIS_DELETE_ICONS_EXTRA
        "Delete '$SMPROGRAMS\\\\$START_MENU\\\\${PROJECT_DISPLAY_NAME}.lnk'"
    )
elseif(APPLE)
    # macOS bundle
    set(CPACK_GENERATOR "DragNDrop")
    set(CPACK_DMG_VOLUME_NAME "${PROJECT_DISPLAY_NAME}")
    set(CPACK_DMG_FORMAT "UDZO")
    set(CPACK_PACKAGE_ICON "${CMAKE_SOURCE_DIR}/assets/icon.icns")
elseif(UNIX)
    # Linux packages
    set(CPACK_GENERATOR "DEB;RPM;TGZ")
    
    # DEB specific
    set(CPACK_DEBIAN_PACKAGE_MAINTAINER "${CPACK_PACKAGE_CONTACT}")
    set(CPACK_DEBIAN_PACKAGE_SECTION "games")
    set(CPACK_DEBIAN_PACKAGE_DEPENDS "libc6, libstdc++6, libgcc1")
    
    # RPM specific
    set(CPACK_RPM_PACKAGE_LICENSE "MIT")
    set(CPACK_RPM_PACKAGE_GROUP "Amusements/Games")
    set(CPACK_RPM_PACKAGE_REQUIRES "glibc, libstdc++, libgcc")
endif()

# Install assets
install(DIRECTORY ${CMAKE_SOURCE_DIR}/assets/
    DESTINATION ${CMAKE_INSTALL_PREFIX}/share/${PROJECT_NAME}/assets
    FILES_MATCHING
    PATTERN "*.png"
    PATTERN "*.jpg"
    PATTERN "*.ttf"
    PATTERN "*.otf"
    PATTERN "*.wav"
    PATTERN "*.ogg"
    PATTERN "*.mp3"
    PATTERN "*.obj"
    PATTERN "*.gltf"
    PATTERN "*.glb"
    PATTERN "*.vs"
    PATTERN "*.fs"
    PATTERN "*.glsl"
    PATTERN ".gitkeep" EXCLUDE
)

# Copy assets to build directory for development
add_custom_command(TARGET ${PROJECT_NAME} POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
    ${CMAKE_SOURCE_DIR}/assets $<TARGET_FILE_DIR:${PROJECT_NAME}>/../assets
)

include(CPack)