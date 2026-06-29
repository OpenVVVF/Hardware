#!/bin/bash

# Run Tests Script - Builds and runs tests on the host machine
set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(dirname "$SCRIPT_DIR")"
BUILD_DIR="${PROJECT_ROOT}/build-tests"
BIN_DIR="${PROJECT_ROOT}/Binaries"

echo "=== Building and Running Host Tests ==="
echo

# Create directories
mkdir -p "$BUILD_DIR"
mkdir -p "$BIN_DIR"

# Step 1: Configure CMake for Host Tests
# This will auto-download GTest if missing
echo "--- Step 1: Configuring CMake for Host ---"
cd "$BUILD_DIR"
cmake -DBUILD_HOST_TESTS=ON "$PROJECT_ROOT"

echo
echo "--- Step 2: Building Tests ---"
cmake --build . --parallel

echo
echo "--- Step 3: Running Tests ---"
# Run the test executable directly
# This keeps it simple: one executable, one run.
if [ -f "${BIN_DIR}/${PROJECT_NAME}-Test" ]; then
    "${BIN_DIR}/${PROJECT_NAME}-Test"
else
    echo "Error: Test executable not found in Binaries/"
    exit 1
fi

echo
echo "=== Tests Complete ==="