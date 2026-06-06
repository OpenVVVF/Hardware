#!/bin/bash

BUILD_DIR="build"

echo "Starting Build..."

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

# 3. Compile the code (This will now be incredibly fast for small edits)
cmake --build $BUILD_DIR -j$(nproc)

# Check if build succeeded
if [ $? -ne 0 ]; then
    echo "Build FAILED. Check the errors above."
    exit 1
fi

echo "Build SUCCESSFUL. Ready to flash."
echo "---------------------------------------"

# 4. Automatically find the generated .elf file
BINARY=$(find $BUILD_DIR -maxdepth 1 -name "*.elf" | head -n 1)

if [ -z "$BINARY" ]; then
    echo "Error: No .elf file found in $BUILD_DIR! Did the build fail silently?"
    exit 1
fi

# 5. Flash using OpenOCD
echo "Flashing $BINARY to Nucleo-H723ZG..."
openocd -f interface/stlink.cfg \
        -f target/stm32h7x.cfg \
        -c "program $BINARY verify reset exit"

if [ $? -eq 0 ]; then
    echo "---------------------------------------"
    echo "ALL DONE: Motor controller updated."
else
    echo "---------------------------------------"
    echo "FLASH FAILED. Is the board plugged in?"
fi
