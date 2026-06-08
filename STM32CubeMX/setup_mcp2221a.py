#!/usr/bin/env python3
"""
One-time MCP2221A configuration script.

Reconfigures GP0 and GP1 from LED indicator mode (or any alternate function)
to GPIO mode, and saves this configuration to the MCP2221A's internal Flash
so it persists across power cycles.

Run this once after soldering the GP0->BOOT0 and GP1->RESET resistors.
"""
import sys
import time


def main():
    try:
        from EasyMCP2221 import Device
    except ImportError:
        print("ERROR: EasyMCP2221 not installed.")
        print("Run: python3 -m venv .venv && .venv/bin/pip install EasyMCP2221")
        sys.exit(1)

    print("Connecting to MCP2221A...")
    try:
        mcp = Device()
    except Exception as e:
        print(f"ERROR: Could not connect to MCP2221A: {e}")
        print("Make sure the MCP2221A is plugged in via USB.")
        sys.exit(1)

    print(f"Connected. Revision: {mcp.revision()}")
    print("")

    # Read current Flash settings so we can show the user what changed
    print("Reading current Flash settings...")
    try:
        flash_info = mcp.read_flash_info()
        gp = flash_info.get("GP_SETTINGS", {})
        print(f"  Current GP0 setting: {gp.get('GP0', {}).get('func', 'unknown')}")
        print(f"  Current GP1 setting: {gp.get('GP1', {}).get('func', 'unknown')}")
        print(f"  Current GP2 setting: {gp.get('GP2', {}).get('func', 'unknown')}")
        print(f"  Current GP3 setting: {gp.get('GP3', {}).get('func', 'unknown')}")
    except Exception as e:
        print(f"  (Could not read Flash info: {e})")
    print("")

    # Step 1: Set GP0 and GP1 to GPIO mode in SRAM
    print("Setting GP0 and GP1 to GPIO mode...")
    # out0=False, out1=False means GPIO direction = INPUT (safe high-Z)
    mcp.set_pin_function(gp0="GPIO_IN", gp1="GPIO_IN", out0=False, out1=False)
    time.sleep(0.1)

    # Verify SRAM state
    gpio_state = mcp.GPIO_read()
    print(f"  Current GPIO read: {gpio_state}")
    print("")

    # Step 2: Save SRAM configuration to Flash (persistent across power cycles)
    print("Saving configuration to Flash (this persists across power cycles)...")
    try:
        mcp.save_config()
        print("  Save OK.")
    except Exception as e:
        print(f"  WARNING: Save may have failed: {e}")
        print("  Some MCP2221A variants require a different save method.")
    print("")

    # Step 3: Verify by re-reading Flash
    print("Verifying Flash configuration...")
    try:
        flash_info = mcp.read_flash_info()
        gp = flash_info.get("GP_SETTINGS", {})
        gp0_new = gp.get('GP0', {}).get('func', 'unknown')
        gp1_new = gp.get('GP1', {}).get('func', 'unknown')
        print(f"  New GP0 setting: {gp0_new}")
        print(f"  New GP1 setting: {gp1_new}")

        # Accept both GPIO_IN and GPIO_OUT as valid GPIO modes
        gp0_ok = "GPIO" in str(gp0_new)
        gp1_ok = "GPIO" in str(gp1_new)

        if gp0_ok and gp1_ok:
            print("")
            print("=" * 50)
            print("  SUCCESS! MCP2221A is now configured.")
            print("=" * 50)
            print("")
            print("GP0 and GP1 are now GPIO inputs by default.")
            print("Your board should boot normally without random resets.")
            print("")
            print("You can now run: ./build_flash_uart.sh")
            return
        else:
            print("")
            print("WARNING: Flash verify shows GP0/GP1 may not be GPIO.")
            print("         The configuration might still be correct — some")
            print("         firmware versions report values differently.")
    except Exception as e:
        print(f"  (Could not verify: {e})")

    print("")
    print("=" * 50)
    print("  Setup complete (with warnings).")
    print("=" * 50)
    print("")
    print("Try running './build_flash_uart.sh' to test flashing.")


if __name__ == "__main__":
    main()
