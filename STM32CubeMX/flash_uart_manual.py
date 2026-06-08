#!/usr/bin/env python3
"""
flash_uart_manual.py

Flash STM32H7 firmware via UART using STM32CubeProgrammer CLI.
Does NOT touch the MCP2221A GPIO — user must manually handle BOOT0 and NRST.

Usage:
    python3 flash_uart_manual.py [firmware.bin]

After running, hold BOOT0 (BOOTSEL) HIGH, press and release RESET,
then press Enter in the terminal to start flashing.
"""

import subprocess
import sys
import os

# Default firmware path (relative to project root)
DEFAULT_FIRMWARE = os.path.join(
    os.path.dirname(os.path.abspath(__file__)),
    "build", "STM32CubeMX.bin"
)

# UART port for MCP2221A CDC on Linux
DEFAULT_PORT = "/dev/ttyACM0"
DEFAULT_BAUDRATE = 460800


def check_stm32_programmer():
    """Verify STM32_Programmer_CLI is available."""
    try:
        subprocess.run(
            ["STM32_Programmer_CLI", "--version"],
            capture_output=True,
            check=True
        )
    except (subprocess.CalledProcessError, FileNotFoundError):
        print("ERROR: STM32_Programmer_CLI not found in PATH.")
        print("Make sure STM32CubeProgrammer CLI is installed and on your PATH.")
        sys.exit(1)


def flash_firmware(firmware_path, port=DEFAULT_PORT, baudrate=DEFAULT_BAUDRATE):
    """Flash firmware via UART."""
    if not os.path.isfile(firmware_path):
        print(f"ERROR: Firmware not found: {firmware_path}")
        print("Build first with: ./build_flash_uart.sh")
        sys.exit(1)

    print(f"Firmware: {firmware_path}")
    print(f"Port:     {port}")
    print(f"Baudrate: {baudrate}")
    print()
    print(">>> MANUAL STEP REQUIRED <<<")
    print("1. Hold BOOT0 (BOOTSEL) button — or ensure BOOT0 is HIGH")
    print("2. Press and release RESET (NRST)")
    print("3. Release BOOT0 after ~100 ms")
    input("Press Enter when the board is in bootloader mode...")
    print()

    cmd = [
        "STM32_Programmer_CLI",
        "-c", f"port={port} br={baudrate}",
        # "-e", "all",
        "-w", firmware_path,
    
        "0x08000000 "
        "-v",
    ]

    print("Running STM32CubeProgrammer CLI...")
    print(" ".join(cmd))
    print()

    result = subprocess.run(cmd)
    return result.returncode


def main():
    check_stm32_programmer()

    if len(sys.argv) > 1:
        firmware_path = sys.argv[1]
    else:
        firmware_path = DEFAULT_FIRMWARE

    return flash_firmware(firmware_path)


if __name__ == "__main__":
    sys.exit(main())
