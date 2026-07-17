"""Board assembly docs: one interactive HTML BOM (iBOM) per board.

InteractiveHtmlBom needs KiCad's pcbnew module, so it runs inside the KiCad
flatpak's python with our venv on PYTHONPATH. Falls back to a host python
when pcbnew is available there.
"""

import subprocess
import sys
from pathlib import Path

from .context import Context

_VENV_SITE = Path(__file__).resolve().parent.parent / ".venv" / "lib"
_GENERATOR = _VENV_SITE.parent.parent / "lib" / "python3.12" / "site-packages" / "InteractiveHtmlBom" / "generate_interactive_bom.py"


def _generator_script() -> Path:
    """Locate generate_interactive_bom.py in the venv (any python3.x)."""
    for sp in sorted(_VENV_SITE.glob("python3*/site-packages/InteractiveHtmlBom/generate_interactive_bom.py")):
        return sp
    raise FileNotFoundError(
        "InteractiveHtmlBom not found in .venv — run: .venv/bin/pip install InteractiveHtmlBom"
    )


def build_ibom(pcb_path: Path, out_dir: Path) -> bool:
    """Generate <Board>.html interactive assembly BOM. Returns True on success."""
    script = _generator_script()
    site = script.parent.parent
    out_dir.mkdir(parents=True, exist_ok=True)
    cmd = [
        "flatpak", "run",
        f"--env=PYTHONPATH={site}",
        "--command=python3", "org.kicad.KiCad",
        str(script),
        "--no-browser",
        "--dest-dir", str(out_dir),
        "--name-format", "%f",
        str(pcb_path),
    ]
    try:
        result = subprocess.run(cmd, capture_output=True, text=True, timeout=600)
    except FileNotFoundError:
        print("  flatpak not found; iBOM needs KiCad (flatpak) on this machine.", file=sys.stderr)
        return False
    out_html = out_dir / f"{pcb_path.stem}.html"
    if result.returncode != 0 or not out_html.is_file():
        tail = (result.stderr or result.stdout).strip().splitlines()[-1:]
        print(f"  iBOM failed for {pcb_path.stem}: {tail}", file=sys.stderr)
        return False
    return True


def build_all(ctx: Context, chassis: str) -> list:
    """iBOM for every board of the chassis. Returns generated paths."""
    boards_dir = ctx.hardware_root / chassis / "Boards"
    out_dir = ctx.hardware_root / chassis / "FabricationData" / "Assembly"
    made = []
    if not boards_dir.is_dir():
        return made
    for board_dir in sorted(boards_dir.iterdir()):
        pcb = board_dir / f"{board_dir.name}.kicad_pcb"
        if board_dir.is_dir() and pcb.is_file():
            if build_ibom(pcb, out_dir):
                made.append(out_dir / f"{board_dir.name}.html")
    return made
