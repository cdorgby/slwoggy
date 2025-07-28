#!/bin/bash

# Build for Windows using MinGW cross-compiler
echo "Building for Windows..."

# First check if MinGW is installed
if ! command -v x86_64-w64-mingw32-gcc &> /dev/null; then
    echo "MinGW cross-compiler not found. Install it with:"
    echo "sudo apt-get install mingw-w64"
    exit 1
fi

mkdir -p build/windows
cd build/windows
cmake ../.. -DCMAKE_TOOLCHAIN_FILE=../../windows-toolchain.cmake -DCMAKE_BUILD_TYPE=Release
make -j$(nproc)
echo "Windows build complete! Binary at: build/windows/bin/RaylibHelloWorld.exe"