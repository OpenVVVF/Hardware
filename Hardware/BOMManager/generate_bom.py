#!/usr/bin/env python3
"""Deprecated shim — use `python3 bom.py generate` instead."""

import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))

from bom_manager.cli import main

if __name__ == "__main__":
    print("Note: generate_bom.py is deprecated; use `python3 bom.py generate`.\n", file=sys.stderr)
    sys.exit(main(["generate"] + sys.argv[1:]))
