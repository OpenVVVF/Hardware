#!/usr/bin/env python3
"""Deprecated shim — use `python3 bom.py import sendcutsend` instead."""

import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))

from bom_manager.cli import main

if __name__ == "__main__":
    print("Note: import_sendcutsend_cart.py is deprecated; use `python3 bom.py import sendcutsend`.\n", file=sys.stderr)
    sys.exit(main(["import", "sendcutsend"] + sys.argv[1:]))
