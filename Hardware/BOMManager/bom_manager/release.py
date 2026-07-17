"""Release package: full generate + one concatenated schematics PDF + upload checklist."""

import subprocess
import sys
from pathlib import Path
from typing import List, Optional

from . import generate
from .context import Context

RELEASE_QTYS = "1,2,3,5,10"


def find_schematic_pdfs(ctx: Context, chassis: str) -> List[Path]:
    """All board then harness schematic PDFs, in a stable order."""
    chassis_dir = ctx.hardware_root / chassis
    boards_dir = chassis_dir / "Boards"
    pdfs: List[Path] = []
    if boards_dir.is_dir():
        for board_dir in sorted(boards_dir.iterdir()):
            pdf = board_dir / f"{board_dir.name}.pdf"
            if board_dir.is_dir() and pdf.is_file():
                pdfs.append(pdf)
    for harness_root in ("Wiring", "Harnesses"):
        harness_dir = chassis_dir / harness_root
        if not harness_dir.is_dir():
            continue
        for part_dir in sorted(harness_dir.iterdir()):
            if not part_dir.is_dir() or part_dir.name.startswith("."):
                continue
            pdf = part_dir / f"{part_dir.name}.pdf"
            if pdf.is_file():
                pdfs.append(pdf)
    return pdfs


def concatenate_pdfs(pdfs: List[Path], out_path: Path) -> bool:
    """Concatenate PDFs with pdfunite (poppler). Returns True on success."""
    if not pdfs:
        return False
    out_path.parent.mkdir(parents=True, exist_ok=True)
    result = subprocess.run(
        ["pdfunite", *[str(p) for p in pdfs], str(out_path)],
        capture_output=True, text=True,
    )
    if result.returncode != 0:
        print(f"pdfunite failed: {result.stderr.strip()}", file=sys.stderr)
        return False
    return True


def run(ctx: Context, chassis: Optional[str], extra_args=None) -> int:
    argv = ["--extra-qtys", RELEASE_QTYS] + list(extra_args or [])
    rc = generate.run(argv, ctx)
    if rc != 0:
        return rc

    chassis_names = [chassis] if chassis else sorted(
        d.name for d in ctx.hardware_root.iterdir()
        if d.is_dir() and d.name.lower().startswith("chassis")
    )
    for ch in chassis_names:
        pdfs = find_schematic_pdfs(ctx, ch)
        out = ctx.hardware_root / ch / "FabricationData" / "Schematics.pdf"
        if concatenate_pdfs(pdfs, out):
            print(f"\nWrote {out} ({len(pdfs)} schematic sets)")
        else:
            print(f"\nNo schematic PDFs concatenated for {ch} (found {len(pdfs)}).")

        fab = ctx.hardware_root / ch / "FabricationData"
        print(f"""
=== {ch} upload checklist ===
  Mouser:      {fab / 'BOMs' / 'mouser_bom.csv'}  -> Mouser BOM tool
  DigiKey:     {fab / 'BOMs' / 'digikey_bom.csv'}  -> DigiKey BOM tool
  McMaster:    {fab / 'McMaster_Order_Paste.txt'}  -> cart 'paste part numbers' box
  SendCutSend: STEP files in Mechanical/Fab/*/ (specs in Pricing_Report.md)
  PCB fab:     {fab / 'PCB_Fab_Zips'}/*.zip -> your PCB vendor
  Schematics:  {out}
  Prices at 1/2/3/5/10 units: see the Quantity Scaling table in Pricing_Report.md
""")
    return 0
