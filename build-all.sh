#!/bin/bash

# Build for all platforms
echo "Building for all platforms..."

# Make scripts executable
chmod +x build-linux.sh
chmod +x build-windows.sh

# Build for Linux
./build-linux.sh

# Build for Windows
./build-windows.sh

echo "All builds complete!"