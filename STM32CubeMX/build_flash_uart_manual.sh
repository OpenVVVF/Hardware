#!/bin/bash
# build_flash_uart_manual.sh
#
# Build firmware and flash via UART using STM32CubeProgrammer CLI.
# User must manually handle BOOT0 and RESET — this script does NOT touch MCP2221A GPIO.
#
# Usage:
#   ./build_flash_uart_manual.sh
#   ./build_flash_uart_manual.sh /dev/ttyACM0

set -euo pipefail

PROJECT_DIR="$(cd "$(dirname "$0")" && pwd)"
BUILD_DIR="$PROJECT_DIR/build"
FIRMWARE_ELF="$BUILD_DIR/STM32CubeMX.elf"
FIRMWARE_BIN="$BUILD_DIR/STM32CubeMX.bin"

PORT="${1:-/dev/ttyACM0}"
BAUDRATE="${2:-115200}"

echo "========================================"
echo "  Build + Flash UART (manual reset)"
echo "========================================"
echo ""

# ---- Build ----
echo "Building firmware..."
cd "$PROJECT_DIR"
if [ -d "build" ]; then
    cmake --build build
else
    echo "Build directory not found. Running initial CMake configure..."
    cmake --preset=default -B build
    cmake --build build
fi

echo ""
echo "Build complete."
echo ""

# ---- Convert ELF to BIN ----
echo "Converting ELF to BIN..."
arm-none-eabi-objcopy -O binary "$FIRMWARE_ELF" "$FIRMWARE_BIN"
ls -lh "$FIRMWARE_BIN"
echo ""

# ---- Flash ----
echo "Flashing via UART (manual reset required)..."
echo "Port: $PORT"
echo "Baudrate: $BAUDRATE"
echo ""

python3 "$PROJECT_DIR/flash_uart_manual.py" "$FIRMWARE_BIN"
