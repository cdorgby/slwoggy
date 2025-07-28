#!/bin/bash

# Package for all platforms
echo "Creating packages for all platforms..."

# Make scripts executable
chmod +x package-linux.sh
chmod +x package-windows.sh
chmod +x package-macos.sh

# Package for Linux
echo "=== Linux Packages ==="
./package-linux.sh
echo ""

# Package for Windows
echo "=== Windows Packages ==="
./package-windows.sh
echo ""

# Note about macOS
echo "=== macOS Package ==="
echo "To create macOS package, run ./package-macos.sh on a macOS system"
echo ""

echo "Package creation complete!"
echo ""
echo "Created packages:"
echo "Linux:"
ls -la build/linux/*.{deb,rpm,tar.gz} 2>/dev/null
echo ""
echo "Windows:"
ls -la build/windows/*.{zip,exe} 2>/dev/null