#!/bin/bash

# Build and Flash script - run from Tools/ directory
set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

echo "=== Build & Flash Pico Project ==="
echo

# Step 1: Build
echo "--- Step 1: Building ---"
"$SCRIPT_DIR/Build.sh"

echo
echo "--- Step 2: Flashing ---"
"$SCRIPT_DIR/Flash.sh"

echo
echo "=== Build & Flash Complete ==="