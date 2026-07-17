"""Regen pipeline: change the design, run one command, everything re-exports.

Per board: schematic BOM CSV, gerbers + drill into Fab/, DRC report, board
STEP model. Per harness: schematic BOM CSV. Then the normal generate runs.
All exports go through KiCad CLI (flatpak).
"""

import subprocess
import sys
from pathlib import Path
from typing import Optional

from . import generate
from .context import Context

_BOM_FIELDS = "Reference,Value,Footprint,QUANTITY,DNP,EXCLUDE_FROM_BOM"
_BOM_LABELS = "Designator,Designation,Footprint,Quantity,DNP,Exclude from BOM"


def _kicad(args, timeout=600) -> subprocess.CompletedProcess:
    cmd = ["flatpak", "run", "--command=kicad-cli", "org.kicad.KiCad"] + args
    try:
        return subprocess.run(cmd, capture_output=True, text=True, timeout=timeout)
    except FileNotFoundError:
        cmd[0:4] = ["kicad-cli"]
        return subprocess.run(cmd, capture_output=True, text=True, timeout=timeout)


def _export_bom(sch: Path, out_csv: Path) -> bool:
    r = _kicad([
        "sch", "export", "bom", str(sch), "-o", str(out_csv),
        "--fields", _BOM_FIELDS, "--labels", _BOM_LABELS,
        "--group-by", "Value", "--exclude-dnp",
    ])
    return r.returncode == 0 and out_csv.is_file()


_FAB_EXTS = {
    ".gbr", ".gbl", ".gtl", ".gbs", ".gts", ".gbo", ".gto", ".gbp", ".gtp",
    ".gba", ".gta", ".gm1", ".gko", ".gml", ".drl",
}


def _export_fab(pcb: Path, fab_dir: Path) -> bool:
    fab_dir.mkdir(parents=True, exist_ok=True)
    # Clean previous fab outputs first: mixed old/new gerber naming in one zip
    # would double-define layers at the fab house.
    for f in fab_dir.iterdir():
        if f.is_file() and (f.suffix.lower() in _FAB_EXTS or f.suffix.lower() in {f".g{i}" for i in range(1, 10)}):
            f.unlink()
    ok = True
    r = _kicad(["pcb", "export", "gerbers", str(pcb), "-o", str(fab_dir)])
    if r.returncode != 0:
        ok = False
    r = _kicad(["pcb", "export", "drill", str(pcb), "-o", str(fab_dir)])
    if r.returncode != 0:
        ok = False
    return ok


def _export_step(pcb: Path, out_step: Path) -> bool:
    r = _kicad(["pcb", "export", "step", str(pcb), "-o", str(out_step),
                "--subst-models", "--force"])
    return r.returncode == 0 and out_step.is_file()


def _run_drc(pcb: Path, out_txt: Path) -> Optional[int]:
    """Run DRC (errors only — warnings are suppressed for now), write the
    report, return the error count (None on failure)."""
    out_txt.parent.mkdir(parents=True, exist_ok=True)
    r = _kicad(["pcb", "drc", str(pcb), "--refill-zones", "--severity-error", "-o", str(out_txt)])
    if not out_txt.is_file():
        return None
    violations = 0
    import re as _re
    m = _re.search(r"Found (\d+) DRC violations", out_txt.read_text(encoding="utf-8", errors="replace"))
    if m:
        violations = int(m.group(1))
    return violations


def run(ctx: Context, chassis: Optional[str], board_filter=None, do_generate: bool = True) -> int:
    chassis_names = [chassis] if chassis else sorted(
        d.name for d in ctx.hardware_root.iterdir()
        if d.is_dir() and d.name.lower().startswith("chassis")
    )
    for ch in chassis_names:
        boards_dir = ctx.hardware_root / ch / "Boards"
        drc_dir = ctx.hardware_root / ch / "FabricationData" / "DRC"
        print(f"\n=== {ch} regen ===")
        if boards_dir.is_dir():
            for board_dir in sorted(boards_dir.iterdir()):
                if not board_dir.is_dir():
                    continue
                name = board_dir.name
                if board_filter and name not in board_filter:
                    continue
                sch = board_dir / f"{name}.kicad_sch"
                pcb = board_dir / f"{name}.kicad_pcb"
                if not sch.is_file() and not pcb.is_file():
                    continue
                marks = []
                if sch.is_file():
                    marks.append("bom ✓" if _export_bom(sch, board_dir / f"{name}.csv") else "bom ✗")
                if pcb.is_file():
                    marks.append("fab ✓" if _export_fab(pcb, board_dir / "Fab") else "fab ✗")
                    marks.append("step ✓" if _export_step(pcb, board_dir / f"{name}.step") else "step ✗")
                    violations = _run_drc(pcb, drc_dir / f"{name}.txt")
                    marks.append("drc clean" if violations == 0 else
                                 (f"drc {violations} error(s)" if violations is not None else "drc ✗"))
                print(f"  {name:<22} {'  '.join(marks)}")

        for harness_root in ("Wiring", "Harnesses"):
            harness_dir = ctx.hardware_root / ch / harness_root
            if not harness_dir.is_dir():
                continue
            for part_dir in sorted(harness_dir.iterdir()):
                sch = part_dir / f"{part_dir.name}.kicad_sch"
                if part_dir.is_dir() and not part_dir.name.startswith(".") and sch.is_file():
                    ok = _export_bom(sch, part_dir / f"{part_dir.name}.csv")
                    print(f"  {part_dir.name:<22} bom {'✓' if ok else '✗'}")

    if do_generate:
        print()
        return generate.run(["--no-prompt"] if not ctx.descriptor_registry.allow_prompt else [], ctx)
    return 0
