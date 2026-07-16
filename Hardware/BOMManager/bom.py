#!/usr/bin/env python3
"""BOM Manager — interactive shell and CLI entry point.

Usage:
    python3 bom.py                # interactive shell
    python3 bom.py generate ...   # one-shot command
"""

import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))

from bom_manager.cli import main

if __name__ == "__main__":
    sys.exit(main())
