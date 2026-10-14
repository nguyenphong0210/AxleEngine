#!/usr/bin/env bash

# Exit immediately if a command exits with a non-zero status
set -e

# Configuration defaults
BUILD_TYPE="Debug"
RUN_APP=false
CLEAN_BUILD=false
BUILD_DIR="build"

# Parse CLI arguments
for arg in "$@"; do
    case $arg in
        --release|-r)
            BUILD_TYPE="Release"
            shift
            ;;
        --run)
            RUN_APP=true
            shift
            ;;
        --clean|-c)
            CLEAN_BUILD=true
            shift
            ;;
        --help|-h)
            echo "Usage: ./build.sh [options]"
            echo "Options:"
            echo "  --release, -r    Build in Release mode (default: Debug)"
            echo "  --clean, -c      Wipe build directory before compiling"
            echo "  --run            Execute AxleEditor after successful compilation"
            echo "  --help, -h       Display this help message"
            exit 0
            ;;
        *)
            ;;
    esac
done

echo "=========================================="
echo " Building Axle Engine [${BUILD_TYPE}]"
echo "=========================================="

# Clean build directory if requested
if [ "$CLEAN_BUILD" = true ] && [ -d "$BUILD_DIR" ]; then
    echo "--> Cleaning existing build directory..."
    rm -rf "$BUILD_DIR"
fi

# Detect core count for parallel builds
NUM_CORES=$(nproc 2>/dev/null || sysctl -n hw.ncpu 2>/dev/null || echo 4)

# 1. Configure step
echo "--> Configuring CMake project..."
cmake -B "$BUILD_DIR" -DCMAKE_BUILD_TYPE="$BUILD_TYPE"

# 2. Build step
echo "--> Compiling targets using ${NUM_CORES} jobs..."
cmake --build "$BUILD_DIR" --config "$BUILD_TYPE" --parallel "$NUM_CORES"

echo "--> Build completed successfully!"

# 3. Optional: Run executable
if [ "$RUN_APP" = true ]; then
    echo "=========================================="
    echo " Running AxleEditor..."
    echo "=========================================="
    
    # Check for executable binary path (Handles both Windows and Linux layouts)
    if [ -f "$BUILD_DIR/Editor/$BUILD_TYPE/AxleEditor.exe" ]; then
        ./"$BUILD_DIR/Editor/$BUILD_TYPE/AxleEditor.exe"
    elif [ -f "$BUILD_DIR/Editor/AxleEditor.exe" ]; then
        ./"$BUILD_DIR/Editor/AxleEditor.exe"
    elif [ -f "$BUILD_DIR/Editor/AxleEditor" ]; then
        ./"$BUILD_DIR/Editor/AxleEditor"
    else
        echo "Error: AxleEditor executable not found in $BUILD_DIR/Editor!"
        exit 1
    fi
fi