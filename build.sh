#!/bin/bash

# Universal build script for slwoggy
# Automatically detects platform and configures appropriately

# Build type configuration
BUILD_TYPE=${1:-Release}

# Validate build type
case "$BUILD_TYPE" in
    Debug|debug)
        BUILD_TYPE="Debug"
        ;;
    Release|release)
        BUILD_TYPE="Release"
        ;;
    MemCheck|memcheck|Memcheck)
        BUILD_TYPE="MemCheck"
        ;;
    Profile|profile)
        BUILD_TYPE="Profile"
        ;;
    *)
        echo "Invalid build type: $BUILD_TYPE"
        echo "Usage: $0 [Debug|Release|MemCheck|Profile]"
        echo "Default: Release"
        exit 1
        ;;
esac

# Detect platform
detect_platform() {
    if [[ "$OSTYPE" == "linux-gnu"* ]]; then
        echo "linux"
    elif [[ "$OSTYPE" == "darwin"* ]]; then
        echo "darwin"
    elif [[ "$OSTYPE" == "msys" ]] || [[ "$OSTYPE" == "cygwin" ]] || [[ "$OSTYPE" == "win32" ]]; then
        echo "windows"
    elif [[ "$OSTYPE" == "freebsd"* ]]; then
        echo "freebsd"
    else
        echo "unknown"
    fi
}

# Detect number of CPU cores
detect_cores() {
    if command -v nproc &> /dev/null; then
        # Linux
        nproc
    elif command -v sysctl &> /dev/null; then
        # macOS/BSD
        sysctl -n hw.ncpu
    elif command -v getconf &> /dev/null; then
        # POSIX fallback
        getconf _NPROCESSORS_ONLN
    elif [[ -f /proc/cpuinfo ]]; then
        # Linux fallback
        grep -c ^processor /proc/cpuinfo
    else
        # Conservative fallback
        echo 4
    fi
}

# Detect compiler
detect_compiler() {
    local platform=$1
    
    # Check for explicitly set compiler
    if [[ -n "$CXX" ]]; then
        echo "Using explicit compiler: $CXX"
        return
    fi
    
    # Platform-specific defaults
    case "$platform" in
        linux)
            if command -v g++ &> /dev/null; then
                export CXX=g++
                export CC=gcc
            elif command -v clang++ &> /dev/null; then
                export CXX=clang++
                export CC=clang
            fi
            ;;
        darwin)
            # macOS defaults to clang
            export CXX=clang++
            export CC=clang
            ;;
        windows)
            # Windows with MinGW or MSVC
            if command -v cl &> /dev/null; then
                echo "Detected MSVC"
                # MSVC handled by CMake
            elif command -v g++ &> /dev/null; then
                export CXX=g++
                export CC=gcc
            fi
            ;;
    esac
    
    if [[ -n "$CXX" ]]; then
        echo "Using compiler: $CXX"
    fi
}

# Main build process
main() {
    local platform=$(detect_platform)
    local cores=$(detect_cores)
    
    if [[ "$platform" == "unknown" ]]; then
        echo "Warning: Unknown platform detected. Defaulting to 'generic' build directory."
        platform="generic"
    fi
    
    echo "==================================="
    echo "Building slwoggy"
    echo "Platform: $platform"
    echo "Build type: $BUILD_TYPE"
    echo "CPU cores: $cores"
    detect_compiler "$platform"
    echo "==================================="
    
    # Create platform-specific build directory
    local build_dir="build"
    mkdir -p "$build_dir"
    cd "$build_dir" || exit 1
    
    # Configure with CMake
    echo "Configuring with CMake..."
    if [[ "$platform" == "windows" ]] && command -v cl &> /dev/null; then
        # MSVC on Windows
        cmake .. -DCMAKE_BUILD_TYPE=$BUILD_TYPE -G "NMake Makefiles"
        if [[ $? -ne 0 ]]; then
            echo "CMake configuration failed"
            exit 1
        fi
        
        echo "Building with NMake..."
        nmake
    else
        # Unix-like systems (Linux, macOS, MinGW on Windows)
        cmake .. -DCMAKE_BUILD_TYPE=$BUILD_TYPE
        if [[ $? -ne 0 ]]; then
            echo "CMake configuration failed"
            exit 1
        fi
        
        echo "Building with make (using $cores cores)..."
        make -j"$cores"
    fi
    
    if [[ $? -eq 0 ]]; then
        echo "==================================="
        echo "Build successful!"
        echo "Executable: $build_dir/bin/slwoggy"
        echo ""
        echo "To run tests: make tests"
        echo "==================================="
    else
        echo "Build failed!"
        exit 1
    fi
}

# Run main function
main