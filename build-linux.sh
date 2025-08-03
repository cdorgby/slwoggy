#!/bin/bash

# Build for Linux with configurable build type
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

echo "Building for Linux (${BUILD_TYPE} mode)..."
mkdir -p build/linux
cd build/linux
cmake ../.. -DCMAKE_BUILD_TYPE=$BUILD_TYPE
# Detect number of cores cross-platform
if command -v nproc &> /dev/null; then
    CORES=$(nproc)
elif command -v sysctl &> /dev/null; then
    CORES=$(sysctl -n hw.ncpu)
else
    CORES=4  # Fallback
fi

make -j$CORES