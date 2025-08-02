#!/bin/bash

# Build for all platforms with configurable build type
BUILD_TYPE=${1:-Release}

# Validate build type
case "$BUILD_TYPE" in
    Debug|debug)
        BUILD_TYPE="Debug"
        ;;
    Release|release)
        BUILD_TYPE="Release"
        ;;
    MemCheck|memcheck|Memcheck)
        BUILD_TYPE="MemCheck"
        ;;
    Profile|profile)
        BUILD_TYPE="Profile"
        ;;
    *)
        echo "Invalid build type: $BUILD_TYPE"
        echo "Usage: $0 [Debug|Release|MemCheck|Profile]"
        echo "Default: Release"
        exit 1
        ;;
esac

echo "Building for all platforms (${BUILD_TYPE} mode)..."

# Make scripts executable
chmod +x build-linux.sh
chmod +x build-windows.sh

# Build for Linux
echo ""
echo "========== Linux Build =========="
./build-linux.sh $BUILD_TYPE

# Build for Windows
echo ""
echo "========== Windows Build =========="
./build-windows.sh $BUILD_TYPE

echo ""
echo "All builds complete!"