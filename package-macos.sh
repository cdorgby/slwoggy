#!/bin/bash

# Package for macOS (DMG)
echo "Creating macOS package..."
echo "Note: This script should be run on a macOS system"

# First build the project
mkdir -p build/macos
cd build/macos
cmake ../.. -DCMAKE_BUILD_TYPE=Release
make -j$(sysctl -n hw.ncpu)

if [ $? -ne 0 ]; then
    echo "Build failed! Cannot create package."
    exit 1
fi

# Create DMG package
echo "Creating DMG package..."
cpack -G DragNDrop

echo ""
echo "macOS package created:"
ls -la *.dmg 2>/dev/null

cd ../..