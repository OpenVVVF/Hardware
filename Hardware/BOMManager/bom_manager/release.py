"""Release package: full generate + full PDF report + assembly HTML + QC forms + upload checklist."""

from typing import Optional

from . import assembly, generate, pdfreport, qc
from .context import Context

RELEASE_QTYS = "1,2,3,5,10"


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
        fab_dir = ctx.hardware_root / ch / "FabricationData"
        out = fab_dir / "Release_Report.pdf"
        print(f"\nBuilding release PDF for {ch} (front matter + schematics + PCB layers + 3D renders)...")
        result = pdfreport.build(ctx, ch, out)
        if result:
            print(f"Wrote {result}")
        else:
            print(f"Release PDF build failed for {ch}.")

        print(f"Building assembly HTML (iBOM)...")
        made = assembly.build_all(ctx, ch)
        print(f"Wrote {len(made)} assembly file(s) to {fab_dir / 'Assembly'}")

        qc_out = qc.build_qc_forms(ctx, ch, fab_dir / "QC_Forms.pdf")
        if qc_out:
            print(f"Wrote {qc_out}")

        print(f"""
=== {ch} upload checklist ===
  Mouser:      {fab_dir / 'BOMs' / 'mouser_bom.csv'}  -> Mouser BOM tool
  DigiKey:     {fab_dir / 'BOMs' / 'digikey_bom.csv'}  -> DigiKey BOM tool
  McMaster:    {fab_dir / 'McMaster_Order_Paste.txt'}  -> cart 'paste part numbers' box
  SendCutSend: STEP files in Mechanical/Fab/*/ (specs in Pricing_Report.md)
  PCB fab:     {fab_dir / 'PCB_Fab_Zips'}/*.zip -> your PCB vendor
  Assembly:    {fab_dir / 'Assembly'}/*.html -> interactive per-board assembly
  QC forms:    {fab_dir / 'QC_Forms.pdf'} -> print & sign per sub-assembly
  Full report: {out}
  Prices at 1/2/3/5/10 units: Pricing_Report.md and the release PDF cover page
""")
    return 0
