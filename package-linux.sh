#!/bin/bash

# Package for Linux (DEB, RPM, and TGZ)
echo "Creating Linux packages..."

# First build the project
./build-linux.sh

if [ $? -ne 0 ]; then
    echo "Build failed! Cannot create packages."
    exit 1
fi

cd build/linux

# Create packages
echo "Creating DEB package..."
cpack -G DEB

echo "Creating RPM package..."
cpack -G RPM

echo "Creating TGZ archive..."
cpack -G TGZ

echo ""
echo "Linux packages created:"
ls -la *.deb *.rpm *.tar.gz 2>/dev/null

cd ../..