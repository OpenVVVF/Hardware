#!/bin/bash

# Clean script - run from Tools/ directory
set -e

BUILD_DIR="../build"
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

echo "=== Cleaning Build Files ==="
echo

if [ -d "$BUILD_DIR" ]; then
    cd "$BUILD_DIR"
    
    # Run make clean
    echo "Running 'make clean'..."
    make clean 2>/dev/null || true
    
    # Remove generated files
    echo "Removing generated files..."
    rm -f *.uf2 *.bin *.hex *.dis *.map
    
    cd "$SCRIPT_DIR"
    echo "✓ Clean complete"
else
    echo "Build directory does not exist, nothing to clean"
fi

echo
echo "=== Clean Complete ==="