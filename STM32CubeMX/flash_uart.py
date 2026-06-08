#!/usr/bin/env python3
"""
STM32H723 UART Firmware Flasher (via MCP2221A USB-to-UART bridge)

Uses the STM32 factory ROM bootloader over USART3.
Requires:
  - BOOT0 pin pulled HIGH during reset
  - MCP2221A connected to USART3 (PB10/PB11)
  - STM32CubeProgrammer CLI installed (comes with STM32CubeCLT)
"""
import subprocess
import sys
import glob
import os
import time

# ---------------------------------------------------------------------------
# Configuration
# ---------------------------------------------------------------------------
BAUDRATE = 460800
FLASH_ADDR = "0x08000000"

# Try common STM32CubeProgrammer CLI paths
CLI_PATHS = [
    "/opt/st/stm32cubeclt_1.21.0/STM32CubeProgrammer/bin/STM32_Programmer_CLI",
    "/opt/st/stm32cubeclt/STM32CubeProgrammer/bin/STM32_Programmer_CLI",
    "STM32_Programmer_CLI",  # if in PATH
    "C:\\Program Files\\STMicroelectronics\\STM32Cube\\STM32CubeProgrammer\\bin\\STM32_Programmer_CLI.exe",
]


def find_cli():
    for p in CLI_PATHS:
        if os.path.isfile(p):
            return p
    # Also try PATH
    for name in ("STM32_Programmer_CLI", "STM32_Programmer_CLI.exe"):
        for path_dir in os.environ.get("PATH", "").split(os.pathsep):
            full = os.path.join(path_dir, name)
            if os.path.isfile(full):
                return full
    return None


def find_mcp2221_port():
    """Auto-detect MCP2221A serial port."""
    ports = []
    if sys.platform.startswith("linux"):
        ports = glob.glob("/dev/ttyACM*") + glob.glob("/dev/ttyUSB*")
    elif sys.platform.startswith("darwin"):
        ports = glob.glob("/dev/tty.usbmodem*") + glob.glob("/dev/tty.usbserial*")
    elif sys.platform.startswith("win"):
        import serial.tools.list_ports
        ports = [p.device for p in serial.tools.list_ports.comports()
                 if "MCP2221" in p.description or "USB Serial" in p.description]

    if len(ports) == 1:
        return ports[0]
    if len(ports) > 1:
        print("Multiple serial ports found:")
        for i, p in enumerate(ports):
            print(f"  [{i}] {p}")
        choice = input("Select port number: ").strip()
        return ports[int(choice)]
    return None


def main():
    if len(sys.argv) < 2:
        print(f"Usage: {sys.argv[0]} <firmware.bin> [port]")
        print("")
        print("  firmware.bin : binary file to flash")
        print("  port         : serial port (auto-detected if omitted)")
        sys.exit(1)

    firmware = os.path.abspath(sys.argv[1])
    if not os.path.isfile(firmware):
        print(f"ERROR: File not found: {firmware}")
        sys.exit(1)

    port = sys.argv[2] if len(sys.argv) > 2 else find_mcp2221_port()
    if not port:
        port = input("Enter serial port (e.g. /dev/ttyACM0 or COM3): ").strip()

    cli = find_cli()
    if not cli:
        print("ERROR: STM32CubeProgrammer CLI not found.")
        print("Make sure STM32CubeCLT is installed and in your PATH.")
        sys.exit(1)

    print("=" * 60)
    print(" STM32H723 UART Firmware Flasher")
    print("=" * 60)
    print(f" Firmware : {firmware}")
    print(f" Port     : {port}")
    print(f" Baudrate : {BAUDRATE}")
    print("")
    print(" STEP 1: Pull BOOT0 HIGH (PH3 -> 3.3V)")
    print(" STEP 2: Press RESET (or power-cycle)")
    input(" Press ENTER when done...")
    print("")

    cmd = [
        cli,
        "-c", f"port={port}", f"br={BAUDRATE}",
        "-w", firmware, FLASH_ADDR,
        "-v",          # verify after write
    ]

    print(" Flashing...")
    print(" ", " ".join(cmd))
    print("")

    result = subprocess.run(cmd, capture_output=True)

    # STM32CubeProgrammer may return non-zero if it tries an unsupported
    # reset, but "Download verified successfully" means flash is good.
    stdout = result.stdout.decode() if result.stdout else ""
    stderr = result.stderr.decode() if result.stderr else ""
    combined = stdout + stderr
    output_ok = "Download verified successfully" in combined

    if result.returncode == 0 or output_ok:
        print("")
        print(" Flash & Verify OK!")
        print("")
        print(" STEP 3: Release BOOT0 (pull LOW / GND)")
        print(" STEP 4: Press RESET to run application")
    else:
        print("")
        print(" Flash FAILED. Check:")
        print("  - BOOT0 is HIGH during reset")
        print("  - Serial port is correct")
        print("  - MCP2221A is connected")
        sys.exit(1)


if __name__ == "__main__":
    main()
