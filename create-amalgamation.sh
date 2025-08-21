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

# Create a temporary build directory to fetch dependencies
TEMP_BUILD="$TOP/.amalgamate-build"
mkdir -p "$TEMP_BUILD"

# Use CMake to fetch dependencies needed for amalgamation
echo "Fetching dependencies for amalgamation..."
cat > "$TEMP_BUILD/CMakeLists.txt" << 'EOF'
cmake_minimum_required(VERSION 3.11)
project(amalgamate_deps)

include(FetchContent)

FetchContent_Declare(
    fmt
    GIT_REPOSITORY https://github.com/fmtlib/fmt.git
    GIT_TAG 10.1.1
    GIT_SHALLOW TRUE
)

FetchContent_Declare(
    taocpp_json
    GIT_REPOSITORY https://github.com/taocpp/json.git
    GIT_TAG 1.0.0-beta.14
    GIT_SHALLOW TRUE
)

FetchContent_MakeAvailable(fmt taocpp_json)
EOF

if ! cmake -B "$TEMP_BUILD/build" -S "$TEMP_BUILD" -DCMAKE_BUILD_TYPE=Release > /dev/null 2>&1; then
    echo "Failed to fetch dependencies for amalgamation"
    rm -rf "$TEMP_BUILD"
    exit 1
fi

# Update amalgamate config to use fetched dependencies
cat > "$TOP/amalgamate/amalgamate-config.json" << EOF
{
    "target": "../amalgamation/slwoggy.hpp",
    "sources": [
        "include/log.hpp"
    ],
    "include_paths": [
        "include",
        "$TEMP_BUILD/build/_deps/fmt-src/include",
        "$TEMP_BUILD/build/_deps/taocpp_json-src/include"
    ]
}
EOF

# Run amalgamate.py from the amalgamate directory
cd "$TOP/amalgamate"
python3 amalgamate.py -c amalgamate-config.json -s "$TOP" -p amalgamate-prologue.hpp
cd "$TOP"

# Clean up temporary build directory
rm -rf "$TEMP_BUILD"

# Restore original amalgamate config
cat > "$TOP/amalgamate/amalgamate-config.json" << 'EOF'
{
    "target": "../amalgamation/slwoggy.hpp",
    "sources": [
        "include/log.hpp"
    ],
    "include_paths": [
        "include"
    ]
}
EOF

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
    echo "  This indicates a bug: the amalgamation should be self-contained and compile with only -Iamalgamation."
    echo "  Please check the error output and report this issue."
fi
