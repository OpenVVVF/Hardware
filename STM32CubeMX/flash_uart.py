#!/usr/bin/env python3
"""
STM32H723 UART Firmware Flasher (via MCP2221A USB-to-UART bridge)

Uses the STM32 factory ROM bootloader over USART3.

With MCP2221A GPIO auto-control (fully automated):
  - GP0 -> BOOT0 (PH3)  : controls bootloader entry
  - GP1 -> NRST         : controls chip reset

Requires:
  - MCP2221A connected to USART3 (PB10/PB11)
  - STM32CubeProgrammer CLI installed (comes with STM32CubeCLT)
  - EasyMCP2221 Python library (optional, for auto GPIO control)
    Install: python3 -m venv .venv && .venv/bin/pip install EasyMCP2221
"""
import subprocess
import sys
import glob
import os
import time
import argparse

# ---------------------------------------------------------------------------
# Configuration
# ---------------------------------------------------------------------------
BAUDRATE = 460800
FLASH_ADDR = "0x08000000"


def drain_serial_port(port, baudrate, timeout_s=0.5):
    """Open the serial port and drain any stale buffered data.

    This flushes out application printf() data that may still be sitting
    in the Linux CDC-ACM TTY buffers or the MCP2221A UART bridge FIFO.
    If pyserial is not installed this is a no-op.
    """
    try:
        import serial
    except ImportError:
        print("[INFO] pyserial not installed, skipping serial drain.")
        return 0

    try:
        with serial.Serial(port, baudrate, timeout=timeout_s) as s:
            # Give the line a moment to settle, then suck out whatever is there.
            time.sleep(0.1)
            stale = s.read(2048)
            count = len(stale)
            if count:
                # Print first few bytes as hex so we can see what was lingering.
                preview = " ".join(f"{b:02x}" for b in stale[:32])
                suffix = "..." if count > 32 else ""
                print(f"[INFO] Drained {count} stale byte(s): {preview}{suffix}")
            else:
                print("[INFO] Serial drain: buffer was clean.")
            return count
    except Exception as e:
        print(f"[WARN] Could not drain serial port: {e}")
        return 0

# Try common STM32CubeProgrammer CLI paths
CLI_PATHS = [
    "/opt/st/stm32cubeclt_1.21.0/STM32CubeProgrammer/bin/STM32_Programmer_CLI",
    "/opt/st/stm32cubeclt/STM32CubeProgrammer/bin/STM32_Programmer_CLI",
    "STM32_Programmer_CLI",  # if in PATH
    "C:\\Program Files\\STMicroelectronics\\STM32Cube\\STM32CubeProgrammer\\bin\\STM32_Programmer_CLI.exe",
]


# ---------------------------------------------------------------------------
# MCP2221A GPIO helper
# ---------------------------------------------------------------------------
class MCP2221GPIO:
    """Control BOOT0 and RESET via MCP2221A GP0/GP1."""

    def __init__(self):
        try:
            from EasyMCP2221 import Device
        except ImportError as e:
            raise ImportError(
                "EasyMCP2221 not installed. "
                "Run: python3 -m venv .venv && .venv/bin/pip install EasyMCP2221"
            ) from e
        self.mcp = Device()

        # Detect if GP0/GP1 are in LED or other alternate mode.
        try:
            flash_info = self.mcp.read_flash_info()
            gp = flash_info.get("GP_SETTINGS", {})
            gp0_func = str(gp.get('GP0', {}).get('func', 'unknown'))
            gp1_func = str(gp.get('GP1', {}).get('func', 'unknown'))
            if "LED" in gp0_func or "LED" in gp1_func:
                print("[WARN] MCP2221A GP0/GP1 are currently in LED mode!")
                print("       They will pulse with UART data and break flashing.")
                print("       Run './setup_mcp2221a.py' first to fix this permanently.")
        except Exception:
            pass

        # Configure GP0 and GP1 as GPIO outputs.
        # Initial state: BOOT0=LOW (normal boot), RESET=HIGH (not in reset).
        self.mcp.set_pin_function(gp0="GPIO_OUT", gp1="GPIO_OUT", out0=False, out1=True)
        # time.sleep(0.05)

    def enter_bootloader(self):
        """Assert BOOT0=HIGH then pulse RESET to enter bootloader."""
        self.mcp.GPIO_write(gp0=True, gp1=False)
        time.sleep(0.1)
        self.mcp.GPIO_write(gp1=True)
        time.sleep(0.1)

    def exit_bootloader(self):
        """Assert BOOT0=LOW then pulse RESET to run application."""
        self.mcp.GPIO_write(gp0=False, gp1=False)
        time.sleep(0.1)
        self.mcp.GPIO_write(gp1=True)
        time.sleep(0.1)

    def release(self):
        """Release both lines to high-Z (input) so physical buttons still work."""
        try:
            self.mcp.set_pin_function(gp0="GPIO_IN", gp1="GPIO_IN", out0=False, out1=False)
        except Exception:
            pass


def find_cli():
    for p in CLI_PATHS:
        if os.path.isfile(p):
            return p
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
        try:
            import serial.tools.list_ports
            ports = [p.device for p in serial.tools.list_ports.comports()
                     if "MCP2221" in p.description or "USB Serial" in p.description]
        except ImportError:
            pass

    if len(ports) == 1:
        return ports[0]
    if len(ports) > 1:
        print("Multiple serial ports found:")
        for i, p in enumerate(ports):
            print(f"  [{i}] {p}")
        choice = input("Select port number: ").strip()
        return ports[int(choice)]
    return None


def run_cli(cli, args):
    """Run STM32CubeProgrammer CLI and return (returncode, stdout, stderr)."""
    cmd = [cli] + args
    print(" ", " ".join(cmd))
    result = subprocess.run(cmd, capture_output=True)
    stdout = result.stdout.decode() if result.stdout else ""
    stderr = result.stderr.decode() if result.stderr else ""
    if stdout:
        print(stdout)
    if stderr:
        print(stderr, file=sys.stderr)
    return result.returncode, stdout, stderr


def main():
    parser = argparse.ArgumentParser(
        description="Flash STM32H723 firmware over UART using built-in ROM bootloader"
    )
    parser.add_argument("firmware", help="Binary firmware file to flash")
    parser.add_argument("port", nargs="?", help="Serial port (auto-detected if omitted)")
    parser.add_argument("--manual", action="store_true",
                        help="Manual mode (press buttons yourself)")
    parser.add_argument("--no-drain", action="store_true",
                        help="Skip the serial port drain step")
    args = parser.parse_args()

    firmware = os.path.abspath(args.firmware)
    if not os.path.isfile(firmware):
        print(f"ERROR: File not found: {firmware}")
        sys.exit(1)

    port = args.port if args.port else find_mcp2221_port()
    if not port:
        port = input("Enter serial port (e.g. /dev/ttyACM0 or COM3): ").strip()

    cli = find_cli()
    if not cli:
        print("ERROR: STM32CubeProgrammer CLI not found.")
        print("Make sure STM32CubeCLT is installed and in your PATH.")
        sys.exit(1)

    gpio = None
    auto_mode = False
    if not args.manual:
        try:
            gpio = MCP2221GPIO()
            auto_mode = True
            print("[AUTO] MCP2221A GPIO control enabled (GP0=BOOT0, GP1=RESET)")
        except Exception as e:
            print(f"[WARN] MCP2221A GPIO auto-control failed: {e}")
            print("[WARN] Falling back to MANUAL mode.")
            print("       Install EasyMCP2221 for full automation:")
            print("       python3 -m venv .venv && .venv/bin/pip install EasyMCP2221")
            print("")

    print("=" * 60)
    print(" STM32H723 UART Firmware Flasher")
    print("=" * 60)
    print(f" Firmware : {firmware}")
    print(f" Port     : {port}")
    print(f" Baudrate : {BAUDRATE}")
    print(f" Mode     : {'AUTO (MCP2221A GPIO)' if auto_mode else 'MANUAL (press buttons)'}")
    print("")

    # -----------------------------------------------------------------------
    # Step 1: Enter bootloader
    # -----------------------------------------------------------------------
    if auto_mode:
        print(" Entering bootloader via MCP2221A GPIO...")
        gpio.enter_bootloader()
    else:
        print(" STEP 1: Pull BOOT0 HIGH (PH3 -> 3.3V)")
        print(" STEP 2: Press RESET (or power-cycle)")
        input(" Press ENTER when done...")

    # Drain stale application data from the serial buffers before the CLI
    # opens the port.  This often eliminates the need for a long delay.
    if not args.no_drain:
        drain_serial_port(port, BAUDRATE)

    print("")

    # -----------------------------------------------------------------------
    # Step 2: Flash + Verify
    # -----------------------------------------------------------------------
    print(" Flashing...")
    ret, stdout, stderr = run_cli(cli, [
        "-c", f"port={port}", f"br={BAUDRATE}",
        "-w", firmware, FLASH_ADDR,
        "-v",
    ])
    combined = stdout + stderr
    flash_ok = (ret == 0) or ("Download verified successfully" in combined)

    if not flash_ok:
        print("")
        print(" Flash FAILED. Check:")
        print("  - BOOT0 is HIGH during reset")
        print("  - Serial port is correct")
        print("  - MCP2221A is connected")
        if gpio:
            gpio.release()
        sys.exit(1)

    print("")
    print(" Flash & Verify OK!")

    # -----------------------------------------------------------------------
    # Step 3: Exit bootloader and run application
    # -----------------------------------------------------------------------
    if auto_mode:
        print("")
        print(" Starting application via MCP2221A GPIO...")
        gpio.exit_bootloader()
        gpio.release()
        print("")
        print(" Application started!")
    else:
        print("")
        print(" STEP 3: Release BOOT0 (pull LOW / GND)")
        print(" STEP 4: Press RESET to run application")


if __name__ == "__main__":
    main()
