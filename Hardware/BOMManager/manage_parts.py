#!/usr/bin/env python3
"""Deprecated shim — the interactive shell replaces this tool.

Use `python3 bom.py` and its commands: parts, add, price, pack, stock,
exclude, mech, rev, fab.
"""

import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))

from bom_manager.cli import main

if __name__ == "__main__":
    print("Note: manage_parts.py is deprecated; starting the `bom.py` shell instead.\n", file=sys.stderr)
    sys.exit(main())
