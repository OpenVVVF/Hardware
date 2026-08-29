"""Release package: full generate + full PDF report + assembly HTML + QC forms + labels + upload checklist."""

from typing import Optional

from . import assembly, generate, labels, pdfreport, qc
from .context import Context
from .variants import load_chassis_variants

RELEASE_QTYS = "1,2,3,5,10"


def _variant_arg(extra_args) -> Optional[str]:
    """First --variant name from generate args, or None (use the YAML default)."""
    args = list(extra_args or [])
    for i, a in enumerate(args):
        if a == "--variant" and i + 1 < len(args):
            return args[i + 1].split(",")[0]
        if a.startswith("--variant="):
            return a.split("=", 1)[1].split(",")[0]
    return None


def run(ctx: Context, chassis: Optional[str], extra_args=None) -> int:
    argv = ["--extra-qtys", RELEASE_QTYS, "--variants"] + list(extra_args or [])
    rc = generate.run(argv, ctx)
    if rc != 0:
        return rc

    # PDFs/QC/labels are built once, for the single --variant if given, else
    # the chassis' default build variant (base tree when no variants.yaml).
    cli_variant = _variant_arg(extra_args)

    chassis_names = [chassis] if chassis else sorted(
        d.name for d in ctx.hardware_root.iterdir()
        if d.is_dir() and d.name.lower().startswith("chassis")
    )
    for ch in chassis_names:
        variant = cli_variant
        if variant is None:
            variant_set = load_chassis_variants(ctx.hardware_root / ch)
            variant = variant_set.default if variant_set else None

        fab_dir = ctx.hardware_root / ch / "FabricationData"
        out = fab_dir / "Release_Report.pdf"
        print(f"\nBuilding release PDF for {ch} (front matter + schematics + PCB layers + 3D renders)...")
        result = pdfreport.build(ctx, ch, out, variant=variant)
        if result:
            print(f"Wrote {result}")
        else:
            print(f"Release PDF build failed for {ch}.")

        print(f"Building assembly HTML (iBOM)...")
        made = assembly.build_all(ctx, ch)
        print(f"Wrote {len(made)} assembly file(s) to {fab_dir / 'Assembly'}")

        qc_out = qc.build_qc_forms(ctx, ch, fab_dir / "QC_Forms.pdf", variant=variant)
        if qc_out:
            print(f"Wrote {qc_out}")

        labels_out = labels.build(ctx, ch, fab_dir / "Labels.pdf", variant=variant)
        if labels_out:
            print(f"Wrote {labels_out}")

        print(f"""
=== {ch} upload checklist ===
  Mouser:      {fab_dir / 'BOMs' / 'mouser_bom.csv'}  -> Mouser BOM tool
  DigiKey:     {fab_dir / 'BOMs' / 'digikey_bom.csv'}  -> DigiKey BOM tool
  McMaster:    {fab_dir / 'McMaster_Order_Paste.txt'}  -> cart 'paste part numbers' box
  SendCutSend: STEP files in Mechanical/Fab/*/ (specs in Pricing_Report.md)
  PCB fab:     {fab_dir / 'PCB_Fab_Zips'}/*.zip -> your PCB vendor
  Assembly:    {fab_dir / 'Assembly'}/*.html -> interactive per-board assembly
  QC forms:    {fab_dir / 'QC_Forms.pdf'} -> print & sign per sub-assembly
  Labels:      {fab_dir / 'Labels.pdf'} -> HV warnings, chassis ID, part labels
  Full report: {out}
  Prices at 1/2/3/5/10 units: Pricing_Report.md and the release PDF cover page
""")
    return 0
