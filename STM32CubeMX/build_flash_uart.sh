#!/bin/bash

BUILD_DIR="build"
BINARY_ELF="$BUILD_DIR/STM32CubeMX.elf"
BINARY_BIN="$BUILD_DIR/STM32CubeMX.bin"

echo "=========================================="
echo "  Build + UART Flash (via MCP2221A)"
echo "=========================================="
echo ""

# 1. Create build directory if it doesn't exist
mkdir -p $BUILD_DIR

# 2. Only run the CMake configuration step if the build files are missing
if [ ! -f "$BUILD_DIR/build.ninja" ]; then
    echo "Running Initial CMake Configuration..."
    
    TOOLCHAIN=$(find . -maxdepth 2 -name "*arm*toolchain*.cmake" -o -name "*gcc*.cmake" | grep -i "arm" | head -n 1)

    if [ -z "$TOOLCHAIN" ]; then
        echo "Warning: No toolchain file found! Forcing ARM compiler manually..."
        CMAKE_ARGS="-DCMAKE_C_COMPILER=arm-none-eabi-gcc -DCMAKE_CXX_COMPILER=arm-none-eabi-g++ -DCMAKE_ASM_COMPILER=arm-none-eabi-gcc -DCMAKE_SYSTEM_NAME=Generic"
    else
        echo "Using Toolchain: $TOOLCHAIN"
        CMAKE_ARGS="-DCMAKE_TOOLCHAIN_FILE=$TOOLCHAIN"
    fi

    cmake -B $BUILD_DIR -G Ninja -DCMAKE_BUILD_TYPE=Debug $CMAKE_ARGS .
fi

# 3. Compile the code
echo ""
echo "Building firmware..."
cmake --build $BUILD_DIR -j$(nproc)

# Check if build succeeded
if [ $? -ne 0 ]; then
    echo ""
    echo "Build FAILED. Check the errors above."
    exit 1
fi

echo ""
echo "Build SUCCESSFUL."

# 4. Convert ELF to raw binary
echo "Converting ELF to BIN..."
arm-none-eabi-objcopy -O binary "$BINARY_ELF" "$BINARY_BIN"

if [ ! -f "$BINARY_BIN" ]; then
    echo "Error: Failed to create $BINARY_BIN"
    exit 1
fi

echo "Binary created: $BINARY_BIN"
echo ""

# 5. Flash via UART using the ROM bootloader
echo "---------------------------------------"
echo "  UART FLASH SEQUENCE"
echo "---------------------------------------"
echo ""
echo "  STEP 1: Hold BOOTSEL button"
echo "  STEP 2: Press and release RESET"
echo "  STEP 3: Release BOOTSEL"
echo ""
read -p "Press ENTER when the board is in bootloader mode..."
echo ""

python3 flash_uart.py "$BINARY_BIN"

if [ $? -ne 0 ]; then
    echo ""
    echo "FLASH FAILED."
    exit 1
fi

echo ""
echo "---------------------------------------"
echo "  ALL DONE"
echo "---------------------------------------"
echo ""
echo "  STEP 4: Make sure BOOTSEL is released"
echo "  STEP 5: Press RESET to run application"
