#!/bin/bash

# Build for Linux
echo "Building for Linux..."
mkdir -p build/linux
cd build/linux
cmake ../.. -DCMAKE_BUILD_TYPE=Release
make -j$(nproc)
echo "Linux build complete! Binary at: build/linux/bin/RaylibHelloWorld"