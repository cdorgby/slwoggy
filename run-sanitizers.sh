#!/bin/bash
# Run all sanitizer configurations and generate reports

set -e

# Colors for output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
NC='\033[0m' # No Color

# Get the OS type
OS_TYPE=$(uname -s | tr '[:upper:]' '[:lower:]')
case "$OS_TYPE" in
    darwin*)
        OS_DIR="darwin"
        LEAK_DETECTION=0  # macOS doesn't support leak detection in ASan
        ;;
    linux*)
        OS_DIR="linux"
        LEAK_DETECTION=1
        ;;
    *)
        OS_DIR="default"
        LEAK_DETECTION=1
        ;;
esac

# Base directory for sanitizer builds (use absolute paths)
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
BASE_BUILD_DIR="$SCRIPT_DIR/build/sanitizers"
REPORT_DIR="$SCRIPT_DIR/sanitizer-reports"
TIMESTAMP=$(date +%Y%m%d_%H%M%S)

# Detect number of cores for parallel build
if command -v nproc &> /dev/null; then
    JOBS=$(nproc)
elif command -v sysctl &> /dev/null; then
    JOBS=$(sysctl -n hw.ncpu)
else
    JOBS=4  # Default fallback
fi

# Create report directory
mkdir -p "$REPORT_DIR"

# Function to run sanitizer test
run_sanitizer() {
    local sanitizer=$1
    local sanitizer_name=$2
    local extra_env=$3
    
    echo -e "\n${YELLOW}=== Testing with $sanitizer_name ===${NC}"
    
    local build_dir="$BASE_BUILD_DIR/$sanitizer"
    local report_file="$REPORT_DIR/${sanitizer}_${TIMESTAMP}.log"
    
    # Configure
    echo "Configuring with $sanitizer_name..."
    cmake -B "$build_dir" \
        -DCMAKE_BUILD_TYPE=Debug \
        -DSANITIZER="$sanitizer" \
        -DCMAKE_EXPORT_COMPILE_COMMANDS=ON \
        > "$report_file" 2>&1 || {
        echo -e "${RED}❌ Configuration failed for $sanitizer_name${NC}"
        echo "See $report_file for details"
        return 1
    }
    
    # Build
    echo "Building with $sanitizer_name (using $JOBS parallel jobs)..."
    cmake --build "$build_dir" --parallel $JOBS >> "$report_file" 2>&1 || {
        echo -e "${RED}❌ Build failed for $sanitizer_name${NC}"
        echo "See $report_file for details"
        return 1
    }
    
    # Build tests
    echo "Building tests with $sanitizer_name..."
    cmake --build "$build_dir" --target tests --parallel $JOBS >> "$report_file" 2>&1 || {
        echo -e "${RED}❌ Test build failed for $sanitizer_name${NC}"
        echo "See $report_file for details"
        return 1
    }
    
    # Run tests
    echo "Running tests with $sanitizer_name..."
    STATUS_FILE="$REPORT_DIR/${sanitizer}_${TIMESTAMP}.status"
    (
        cd "$build_dir"
        eval "$extra_env"
        if ctest --output-on-failure >> "$report_file" 2>&1; then
            echo -e "${GREEN}✅ $sanitizer_name: All tests passed${NC}"
            echo "PASS" > "$STATUS_FILE"
        else
            # Check if it's a sanitizer error or test failure
            if grep -q "ERROR: AddressSanitizer\|ERROR: ThreadSanitizer\|ERROR: MemorySanitizer\|ERROR: UndefinedBehaviorSanitizer\|WARNING: ThreadSanitizer: data race" "$report_file"; then
                echo -e "${RED}❌ $sanitizer_name: Sanitizer errors detected!${NC}"
                echo "SANITIZER_ERROR" > "$STATUS_FILE"
                
                # Extract sanitizer errors
                echo -e "\n${RED}Sanitizer Error Summary:${NC}"
                grep -A 5 "ERROR: " "$report_file" | head -20
            else
                echo -e "${YELLOW}⚠️  $sanitizer_name: Test failures (not sanitizer errors)${NC}"
                echo "TEST_FAIL" > "$STATUS_FILE"
            fi
            echo "See $report_file for full details"
            return 1
        fi
    )
}

# Suppression files are in the script directory

# Array of sanitizers to test with suppressions
if [ "$LEAK_DETECTION" -eq 1 ]; then
    # Linux with leak detection
    declare -a sanitizers=(
        "address,undefined|AddressSanitizer + UBSan|ASAN_OPTIONS=detect_leaks=1:check_initialization_order=1:strict_init_order=1:halt_on_error=0:suppressions=${SCRIPT_DIR}/sanitizers/asan.supp LSAN_OPTIONS=suppressions=${SCRIPT_DIR}/sanitizers/lsan.supp UBSAN_OPTIONS=print_stacktrace=1:halt_on_error=0:suppressions=${SCRIPT_DIR}/sanitizers/ubsan.supp"
        "thread|ThreadSanitizer|TSAN_OPTIONS=halt_on_error=0:exitcode=0:history_size=7:suppressions=${SCRIPT_DIR}/sanitizers/tsan.supp"
    )
else
    # macOS without leak detection
    declare -a sanitizers=(
        "address,undefined|AddressSanitizer + UBSan|ASAN_OPTIONS=detect_leaks=0:check_initialization_order=1:strict_init_order=1:halt_on_error=0:suppressions=${SCRIPT_DIR}/sanitizers/asan.supp UBSAN_OPTIONS=print_stacktrace=1:halt_on_error=0:suppressions=${SCRIPT_DIR}/sanitizers/ubsan.supp"
        "thread|ThreadSanitizer|TSAN_OPTIONS=halt_on_error=0:exitcode=0:history_size=7:suppressions=${SCRIPT_DIR}/sanitizers/tsan.supp"
    )
fi

# Add MemorySanitizer only for Clang
if command -v clang++ &> /dev/null && [[ "$OS_TYPE" == "linux" ]]; then
    sanitizers+=("memory|MemorySanitizer|MSAN_OPTIONS=halt_on_error=0:suppressions=${SCRIPT_DIR}/sanitizers/msan.supp")
fi

# Track overall results
total=0
passed=0
failed=0

echo -e "${YELLOW}Starting sanitizer test suite${NC}"
echo "OS: $OS_TYPE"
echo "Reports will be saved to: $REPORT_DIR"
echo "Timestamp: $TIMESTAMP"

# Run each sanitizer
for entry in "${sanitizers[@]}"; do
    IFS='|' read -r sanitizer name env <<< "$entry"
    total=$((total + 1))
    
    if run_sanitizer "$sanitizer" "$name" "$env"; then
        passed=$((passed + 1))
    else
        failed=$((failed + 1))
    fi
done

# Generate summary report
summary_file="$REPORT_DIR/summary_${TIMESTAMP}.txt"
{
    echo "Sanitizer Test Summary"
    echo "====================="
    echo "Date: $(date)"
    echo "OS: $OS_TYPE"
    echo ""
    echo "Results:"
    echo "  Total: $total"
    echo "  Passed: $passed"
    echo "  Failed: $failed"
    echo ""
    echo "Individual Results:"
    
    for entry in "${sanitizers[@]}"; do
        IFS='|' read -r sanitizer name env <<< "$entry"
        status_file="$REPORT_DIR/${sanitizer}_${TIMESTAMP}.status"
        if [ -f "$status_file" ]; then
            status=$(cat "$status_file")
            echo "  - $name: $status"
        else
            echo "  - $name: NOT RUN"
        fi
    done
} > "$summary_file"

# Display summary
echo -e "\n${YELLOW}=== Summary ===${NC}"
cat "$summary_file"

# Exit code based on results
if [ "$failed" -eq 0 ]; then
    echo -e "\n${GREEN}✅ All sanitizer tests passed!${NC}"
    exit 0
else
    echo -e "\n${RED}❌ $failed sanitizer test(s) failed${NC}"
    echo "Check individual reports in $REPORT_DIR for details"
    exit 1
fi