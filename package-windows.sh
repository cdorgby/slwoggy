#!/bin/bash

# Package for Windows (NSIS installer and ZIP)
echo "Creating Windows packages..."

# Check if NSIS is installed
if ! command -v makensis &> /dev/null; then
    echo "Warning: NSIS not found. Only ZIP package will be created."
    echo "To create installer, install NSIS with: sudo apt-get install nsis"
    NSIS_AVAILABLE=0
else
    NSIS_AVAILABLE=1
fi

# First build the project
./build-windows.sh

if [ $? -ne 0 ]; then
    echo "Build failed! Cannot create packages."
    exit 1
fi

cd build/windows

# Create ZIP package
echo "Creating ZIP package..."
cpack -G ZIP

# Create NSIS installer if available
if [ $NSIS_AVAILABLE -eq 1 ]; then
    echo "Creating NSIS installer..."
    cpack -G NSIS
fi

echo ""
echo "Windows packages created:"
ls -la *.zip *.exe 2>/dev/null

cd ../..