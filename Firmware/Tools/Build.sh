#!/bin/bash

# Build script - run from Tools/ directory
set -e

PROJECT_NAME="pico_project"
BUILD_DIR="../build"
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(dirname "$SCRIPT_DIR")"

echo "=== Building Pico Project ==="
echo "Project root: $PROJECT_ROOT"
echo "Build directory: $BUILD_DIR"
echo

# Check if build directory exists
if [ ! -d "$BUILD_DIR" ]; then
    echo "Creating build directory..."
    mkdir -p "$BUILD_DIR"
fi

# Run CMake if needed
cd "$BUILD_DIR"
if [ ! -f "CMakeCache.txt" ]; then
    echo "Running CMake configuration..."
    cmake ..
fi

# Build the project
echo "Compiling..."
make -j$(nproc)

# Verify UF2 file exists
UF2_FILE="${PROJECT_NAME}.uf2"
if [ -f "$UF2_FILE" ]; then
    echo "✓ Build successful! UF2 file created:"
    ls -lh "$UF2_FILE"
else
    echo "✗ ERROR: UF2 file not found at $BUILD_DIR/$UF2_FILE"
    echo "Contents of build directory:"
    ls -la
    exit 1
fi

cd "$SCRIPT_DIR"
echo
echo "=== Build Complete ==="