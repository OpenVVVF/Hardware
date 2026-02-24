#!/bin/bash

# Flash script - run from Tools/ directory
set -e

PROJECT_NAME="pico_project"
BUILD_DIR="../build"
UF2_PATH="${BUILD_DIR}/${PROJECT_NAME}.uf2"
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

echo "=== Flashing Pico Device ==="
echo

# Verify UF2 file exists with absolute path
if [ ! -f "$UF2_PATH" ]; then
    echo "✗ ERROR: UF2 file not found"
    echo "Expected location: $UF2_PATH"
    echo
    echo "Please build the project first with: ./Build.sh"
    exit 1
fi

echo "✓ UF2 file found: $(basename "$UF2_PATH")"
ls -lh "$UF2_PATH"
echo

# Wait for device
echo "Waiting for RP2350 drive..."

# Method 1: Try software flash first
if [ -w "/dev/ttyACM0" ]; then
    echo "Attempting software-triggered flash..."
    echo "Sending 'flash' command..."
    echo -e "flash\n" > /dev/ttyACM0 2>/dev/null || true
    sleep 2
fi

# Method 2: Wait for bootloader drive
while true; do
    MOUNT_PATH=$(findmnt -n -o TARGET --source LABEL=RP2350 2>/dev/null || true)
    
    if [ -n "$MOUNT_PATH" ]; then
        echo "✓ Pico detected at: $MOUNT_PATH"
        echo "Copying firmware..."
        cp "$UF2_PATH" "$MOUNT_PATH/"
        sync
        
        echo "✓ File copied, waiting for reboot..."
        while findmnt -n -o TARGET --source LABEL=RP2350 >/dev/null 2>&1; do
            sleep 0.1
        done
        
        echo
        echo "✓✓✓ Flash Complete! Device is rebooting ✓✓✓"
        break
    fi
    
    echo -n "."
    sleep 0.5
done

echo
cd "$SCRIPT_DIR"