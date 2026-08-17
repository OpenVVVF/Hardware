#!/bin/bash

BUILD_DIR="build"
BINARY_ELF="$BUILD_DIR/STM32CubeMX.elf"
BINARY_BIN="$BUILD_DIR/STM32CubeMX.bin"
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

# Use venv python if available (has EasyMCP2221 installed)
if [ -f "$SCRIPT_DIR/.venv/bin/python" ]; then
    PYTHON="$SCRIPT_DIR/.venv/bin/python"
else
    PYTHON="python3"
fi

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

# 5. Flash. If the InverterClientImGui app is running, its HTTP flash server
#    owns the serial port: hand the binary to it so the client can stay open.
CLIENT_FLASH_URL="${INVERTER_CLIENT_FLASH_URL:-http://localhost:18080}"

flash_state() {  # prints the "state" field from the client's status JSON
    curl -s -m 3 "$CLIENT_FLASH_URL/flash/status" | grep -o '"state":"[^"]*"' | cut -d'"' -f4
}

flash_via_client() {
    echo "Inverter client detected at $CLIENT_FLASH_URL - flashing via its HTTP API"

    local code
    code=$(curl -s -o /dev/null -w "%{http_code}" -m 30 \
        -X POST -H "Content-Type: application/octet-stream" \
        --data-binary @"$BINARY_BIN" "$CLIENT_FLASH_URL/flash")
    if [ "$code" != "202" ]; then
        echo "Client rejected the flash request (HTTP $code)"
        return 1
    fi
    echo "Flash job accepted by client; waiting for completion..."

    # Wait for the job to start (busy=true) so we don't read a stale state.
    local i status
    for i in $(seq 1 20); do
        status=$(curl -s -m 3 "$CLIENT_FLASH_URL/flash/status")
        echo "$status" | grep -q '"busy":true' && break
        sleep 0.5
    done

    # Wait for completion.
    local state
    for i in $(seq 1 120); do
        state=$(flash_state)
        case "$state" in
            Done|Failed)
                if ! curl -s -m 3 "$CLIENT_FLASH_URL/flash/status" | grep -q '"busy":true'; then
                    break
                fi
                ;;
        esac
        sleep 1
    done

    echo "Client flash state: $state"
    if [ "$state" != "Done" ]; then
        curl -s -m 3 "$CLIENT_FLASH_URL/flash/status" | grep -o '"last_error":"[^"]*"'
        return 1
    fi
    return 0
}

CLIENT_RUNNING=0
if command -v curl >/dev/null; then
    probe=$(curl -s -o /dev/null -w "%{http_code}" -m 3 "$CLIENT_FLASH_URL/flash/status")
    [ "$probe" != "000" ] && CLIENT_RUNNING=1
fi

if [ "$CLIENT_RUNNING" = "1" ]; then
    if ! flash_via_client; then
        echo ""
        echo "FLASH FAILED (via client). Check the client's firmware update panel for details."
        exit 1
    fi
else
    echo "---------------------------------------"
    echo "  UART FLASH SEQUENCE"
    echo "---------------------------------------"
    echo ""
    echo "  Python: $PYTHON"
    echo ""

    $PYTHON flash_uart.py "$BINARY_BIN"

    if [ $? -ne 0 ]; then
        echo ""
        echo "FLASH FAILED."
        echo ""
        echo "Troubleshooting:"
        echo "  - Make sure MCP2221A is plugged in"
        echo "  - Try manual mode: $PYTHON flash_uart.py $BINARY_BIN --manual"
        exit 1
    fi
fi

echo ""
echo "---------------------------------------"
echo "  ALL DONE"
echo "---------------------------------------"
