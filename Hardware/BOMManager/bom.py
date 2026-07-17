#!/usr/bin/env python3
"""BOM Manager — interactive shell and CLI entry point.

Usage:
    python3 bom.py                # interactive shell
    python3 bom.py <command> ...  # one-shot command

If a .venv exists next to this file, the tool re-executes itself with the
venv's python so all dependencies (cadquery, matplotlib, reportlab, pandas)
are available regardless of the system python.
"""

import os
import sys
from pathlib import Path

_ROOT = Path(__file__).resolve().parent
_VENV_PY = _ROOT / ".venv" / "bin" / "python"

if _VENV_PY.is_file() and Path(sys.prefix) != _ROOT / ".venv":
    # Re-exec with the venv interpreter (binary may be a symlink to the system
    # python — sys.prefix is what actually tells us we're not in the venv).
    os.execv(str(_VENV_PY), [str(_VENV_PY), str(Path(__file__).resolve())] + sys.argv[1:])

sys.path.insert(0, str(_ROOT))

from bom_manager.cli import main

if __name__ == "__main__":
    sys.exit(main())
