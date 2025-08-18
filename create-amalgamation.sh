#!/bin/bash

# create-amalgamation.sh - Generate single-header amalgamation of slwoggy
# Copyright (c) 2025 dorgby.net. Licensed under MIT License.

set -e

# Get the top-level directory
TOP="$(cd "$(dirname "$0")" && pwd)"

# Create amalgamation directory if it doesn't exist
mkdir -p "$TOP/amalgamation"

# Get version from git tags
VERSION=$(git describe --tags --always --dirty 2>/dev/null || echo "dev")
echo "Creating amalgamation header: amalgamation/slwoggy.hpp (version: $VERSION)"

# Run amalgamate.py from the amalgamate directory
cd "$TOP/amalgamate"
python3 amalgamate.py -c amalgamate-config.json -s "$TOP" -p amalgamate-prologue.hpp
cd "$TOP"

# Replace version placeholder with actual version
# First, escape any special characters in version string for sed
VERSION_ESCAPED=$(echo "$VERSION" | sed 's/[[\.*^$()+?{|]/\\&/g')
# Update the version string definition
sed -i.bak "s/#define SLWOGGY_VERSION_STRING \"dev\"/#define SLWOGGY_VERSION_STRING \"$VERSION_ESCAPED\"/" "$TOP/amalgamation/slwoggy.hpp"
# Also add version info to the header comment
sed -i.bak "s/ \* Generated on:/ * Version: $VERSION_ESCAPED\n * Generated on:/" "$TOP/amalgamation/slwoggy.hpp"
rm -f "$TOP/amalgamation/slwoggy.hpp.bak"

# Get file stats
echo "File size: $(wc -c < "$TOP/amalgamation/slwoggy.hpp") bytes"
echo "Line count: $(wc -l < "$TOP/amalgamation/slwoggy.hpp") lines"

# Test compilation
echo ""
echo "Testing amalgamation..."
cat > "$TOP/test_amalgamation.cpp" << 'EOF'
#include "slwoggy.hpp"

using namespace slwoggy;

int main() {
    LOG(info) << "Amalgamation test successful!" << endl;
    return 0;
}
EOF

if g++ -std=c++20 -I"$TOP/amalgamation" -c "$TOP/test_amalgamation.cpp" -o "$TOP/test_amalgamation.o" 2>/dev/null; then
    echo "✓ Amalgamation compiles successfully"
    rm -f "$TOP/test_amalgamation.cpp" "$TOP/test_amalgamation.o"
else
    echo "✗ Amalgamation compilation failed"
    echo "  Run: g++ -std=c++20 -Iamalgamation -Ithird_party/fmt/include -Ithird_party/taocpp-json/include -c test_amalgamation.cpp"
    echo "  This indicates a bug: the amalgamation should be self-contained and compile with only -Iamalgamation."
    echo "  Please check the error output and report this issue."
fi
