#!/bin/bash

# create-amalgamation.sh - Generate single-header amalgamation of slwoggy
# Copyright (c) 2025 dorgby.net. Licensed under MIT License.

set -e

# Get version from git tags
VERSION=$(git describe --tags --always --dirty 2>/dev/null || echo "dev")
echo "Creating amalgamation header: include/slwoggy.hpp (version: $VERSION)"

# Run amalgamate.py from the amalgamate directory
cd amalgamate
python3 amalgamate.py -c amalgamate-config.json -s .. -p amalgamate-prologue.hpp
cd ..

# Replace version placeholder with actual version
# First, escape any special characters in version string for sed
VERSION_ESCAPED=$(echo "$VERSION" | sed 's/[[\.*^$()+?{|]/\\&/g')
# Update the version string definition
sed -i.bak "s/#define SLWOGGY_VERSION_STRING \"dev\"/#define SLWOGGY_VERSION_STRING \"$VERSION_ESCAPED\"/" include/slwoggy.hpp
# Also add version info to the header comment
sed -i.bak "s/ \* Generated on:/ * Version: $VERSION_ESCAPED\n * Generated on:/" include/slwoggy.hpp
rm -f include/slwoggy.hpp.bak

# Get file stats
echo "File size: $(wc -c < include/slwoggy.hpp) bytes"
echo "Line count: $(wc -l < include/slwoggy.hpp) lines"

# Test compilation
echo ""
echo "Testing amalgamation..."
cat > test_amalgamation.cpp << 'EOF'
#include "include/slwoggy.hpp"

using namespace slwoggy;

int main() {
    LOG(info) << "Amalgamation test successful!" << endl;
    return 0;
}
EOF

if g++ -std=c++20 -c test_amalgamation.cpp -o test_amalgamation.o 2>/dev/null; then
    echo "✓ Amalgamation compiles successfully"
    rm -f test_amalgamation.cpp test_amalgamation.o
else
    echo "✗ Amalgamation compilation failed"
    echo "  Run: g++ -std=c++20 -c test_amalgamation.cpp"
    echo "  to see errors"
fi