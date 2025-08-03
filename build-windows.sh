#!/bin/bash

# Build for Windows using MinGW cross-compiler with configurable build type
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
        echo "Warning: MemCheck build with sanitizers may not work properly on Windows/MinGW"
        echo "Consider using Debug mode instead for Windows builds"
        ;;
    Profile|profile)
        BUILD_TYPE="Profile"
        echo "Note: Some profiling features may be limited on Windows/MinGW"
        ;;
    *)
        echo "Invalid build type: $BUILD_TYPE"
        echo "Usage: $0 [Debug|Release|MemCheck|Profile]"
        echo "Default: Release"
        exit 1
        ;;
esac

echo "Building for Windows (${BUILD_TYPE} mode)..."

# First check if MinGW is installed
if ! command -v x86_64-w64-mingw32-gcc &> /dev/null; then
    echo "MinGW cross-compiler not found. Install it with:"
    echo "sudo apt-get install mingw-w64"
    exit 1
fi

mkdir -p build/windows
cd build/windows
cmake ../.. -DCMAKE_TOOLCHAIN_FILE=../../windows-toolchain.cmake -DCMAKE_BUILD_TYPE=$BUILD_TYPE
# Detect number of cores cross-platform
if command -v nproc &> /dev/null; then
    CORES=$(nproc)
elif command -v sysctl &> /dev/null; then
    CORES=$(sysctl -n hw.ncpu)
else
    CORES=4  # Fallback
fi

make -j$CORES
