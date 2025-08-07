#!/bin/bash
# Quick sanitizer runner for development

set -e

# Default to ASan+UBSan if no argument provided
SANITIZER="${1:-address,undefined}"

# Colors for output
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
NC='\033[0m'

# Get the OS type
OS_TYPE=$(uname -s | tr '[:upper:]' '[:lower:]')
case "$OS_TYPE" in
    darwin*)
        BUILD_DIR="build/darwin-san"
        LEAK_DETECTION=0
        ;;
    linux*)
        BUILD_DIR="build/linux-san"
        LEAK_DETECTION=1
        ;;
    *)
        BUILD_DIR="build/san"
        LEAK_DETECTION=1
        ;;
esac

echo -e "${YELLOW}Building with sanitizer: $SANITIZER${NC}"

# Configure
cmake -B "$BUILD_DIR" \
    -DCMAKE_BUILD_TYPE=Debug \
    -DSANITIZER="$SANITIZER" \
    -DCMAKE_EXPORT_COMPILE_COMMANDS=ON

# Detect number of cores for parallel build
if command -v nproc &> /dev/null; then
    JOBS=$(nproc)
elif command -v sysctl &> /dev/null; then
    JOBS=$(sysctl -n hw.ncpu)
else
    JOBS=4  # Default fallback
fi

# Build with parallel jobs
cmake --build "$BUILD_DIR" --parallel $JOBS

# Build tests if requested
if [ "${2}" == "test" ] || [ "${2}" == "tests" ]; then
    echo -e "${YELLOW}Building tests...${NC}"
    cmake --build "$BUILD_DIR" --target tests --parallel $JOBS
    
    echo -e "${YELLOW}Running tests with sanitizer...${NC}"
    
    # Set environment based on sanitizer type with suppressions
    SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
    if [ "$LEAK_DETECTION" -eq 1 ]; then
        # Linux: use both ASan and LSan suppressions
        export ASAN_OPTIONS="detect_leaks=1:check_initialization_order=1:strict_init_order=1:halt_on_error=0:print_stats=1:suppressions=${SCRIPT_DIR}/sanitizers/asan.supp"
        export LSAN_OPTIONS="suppressions=${SCRIPT_DIR}/sanitizers/lsan.supp"
    else
        # macOS: no leak detection
        export ASAN_OPTIONS="detect_leaks=0:check_initialization_order=1:strict_init_order=1:halt_on_error=0:print_stats=1:suppressions=${SCRIPT_DIR}/sanitizers/asan.supp"
    fi
    export UBSAN_OPTIONS="print_stacktrace=1:halt_on_error=0:suppressions=${SCRIPT_DIR}/sanitizers/ubsan.supp"
    export TSAN_OPTIONS="halt_on_error=0:history_size=7:suppressions=${SCRIPT_DIR}/sanitizers/tsan.supp"
    export MSAN_OPTIONS="halt_on_error=0:suppressions=${SCRIPT_DIR}/sanitizers/msan.supp"
    
    cd "$BUILD_DIR"
    ctest --output-on-failure
    cd - > /dev/null
    
    echo -e "${GREEN}✅ Tests passed with $SANITIZER${NC}"
else
    echo -e "${GREEN}✅ Build complete${NC}"
    echo ""
    echo "To run the program with sanitizers:"
    SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
    echo "  ASAN_OPTIONS=detect_leaks=${LEAK_DETECTION}:halt_on_error=0:suppressions=${SCRIPT_DIR}/sanitizers/asan.supp $BUILD_DIR/bin/slwoggy"
    echo ""
    echo "To run tests:"
    echo "  $0 $SANITIZER test"
fi