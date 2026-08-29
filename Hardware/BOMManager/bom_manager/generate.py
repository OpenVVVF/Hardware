"""Generate consolidated BOMs, price reports, and PCB fab zip bundles."""

import argparse
import csv
import json
import re
import sys
import zipfile
from pathlib import Path

from . import fab
from .addpart import collect_items
from .bom import aggregate_bom
from .context import Context
from .descriptor_registry import DescriptorRegistry
from .part_numbers import line_identity
from .pricing import PricingEngine, generate_markdown_report, line_total
from .suggest import interactive_suggest
from .variants import apply_variant, load_chassis_variants


def parse_args(argv=None) -> argparse.Namespace:
    parser = argparse.ArgumentParser(prog="generate", description="Generate consolidated hardware BOMs.")
    parser.add_argument(
        "--hardware-root",
        type=Path,
        default=None,
        help="Root Hardware directory (default: parent of BOMManager)",
    )
    parser.add_argument(
        "--chassis",
        type=str,
        default=None,
        help="Comma-separated chassis names to include (default: all)",
    )
    parser.add_argument(
        "--board",
        type=str,
        default=None,
        help="Comma-separated board names to include (default: all)",
    )
    parser.add_argument(
        "--qty",
        type=int,
        default=1,
        help="Build quantity for scaling (default: 1)",
    )
    parser.add_argument(
        "--spares",
        choices=["none", "cheap", "all"],
        default="none",
        help="Spares policy (default: none)",
    )
    parser.add_argument(
        "--spares-pct",
        type=float,
        default=10.0,
        help="Percentage extra for --spares all (default: 10)",
    )
    parser.add_argument(
        "--vendors",
        type=str,
        default="all",
        help="Comma-separated vendors to include in report (default: all)",
    )
    parser.add_argument(
        "--output-dir",
        type=Path,
        default=None,
        help="Override output directory (default: Hardware/<Chassis>/FabricationData)",
    )
    parser.add_argument(
        "--refresh-prices",
        action="store_true",
        help="Ignore cached prices and re-query vendors",
    )
    parser.add_argument(
        "--suggest",
        action="store_true",
        help="Interactively suggest parts for missing entries",
    )
    parser.add_argument(
        "--extra-qtys",
        type=str,
        default="3,5,10,25",
        help="Comma-separated quantities for scaling table",
    )
    parser.add_argument(
        "--variants",
        action="store_true",
        help="Also write spares-variant BOMs (standard/generous) under Variants/",
    )
    parser.add_argument(
        "--variant",
        type=str,
        default=None,
        help="Comma-separated build variants to generate (default: all declared "
             "in the chassis variants.yaml)",
    )
    parser.add_argument(
        "--no-pcb-zips",
        action="store_true",
        help="Skip creating PCB fab zip bundles",
    )
    parser.add_argument(
        "--no-prompt",
        action="store_true",
        help="Error if a descriptor is missing instead of prompting (useful for CI)",
    )
    return parser.parse_args(argv)


def write_vendor_csv(lines, vendor: str, path: Path, qty: int = 1) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    with open(path, "w", encoding="utf-8", newline="") as f:
        writer = csv.writer(f)
        if vendor == "mouser":
            # Column order matches the format Mouser's BOM tool expects for
            # spreadsheet upload: Mouser PN first, qty second. Headers are kept
            # simple so Mouser's auto-mapping picks them up reliably.
            writer.writerow(["Mouser Part Number", "qty", "Manufacturer Part Number", "Customer Part Number", "Description"])
            for line in lines:
                if line.primary_vendor() == vendor:
                    order = line.packs_needed(line.quantity * qty)
                    if order <= 0:
                        continue  # covered by on-hand stock
                    writer.writerow([
                        line.vendor_part_number(vendor),
                        order,
                        line.designation,
                        line.internal_pn,
                        line.description,
                    ])
        elif vendor == "digikey":
            writer.writerow(["Quantity", "Digi-Key Part Number", "Manufacturer Part Number", "Description", "Customer Reference"])
            for line in lines:
                if line.primary_vendor() == vendor:
                    order = line.packs_needed(line.quantity * qty)
                    if order <= 0:
                        continue  # covered by on-hand stock
                    writer.writerow([
                        order,
                        line.vendor_part_number(vendor),
                        line.designation,
                        line.description,
                        line.internal_pn,
                    ])
        else:
            writer.writerow(["Quantity", "Internal P/N", "Description", "Customer Part No.", "Manufacturer Part No.", "Vendor", "Vendor P/N"])
            for line in lines:
                if line.primary_vendor() == vendor:
                    order = line.packs_needed(line.quantity * qty)
                    if order <= 0:
                        continue  # covered by on-hand stock
                    writer.writerow([
                        order,
                        line.internal_pn,
                        line.description,
                        line.customer_part,
                        line.designation,
                        vendor,
                        line.vendor_part_number(vendor),
                    ])


def write_mcmaster_order_paste(lines, path: Path, qty: int = 1) -> None:
    """Write McMaster 'Paste part numbers and quantities' format: PN,Qty\n."""
    path.parent.mkdir(parents=True, exist_ok=True)
    with open(path, "w", encoding="utf-8", newline="") as f:
        for line in lines:
            if line.primary_vendor() == "mcmaster":
                pn = line.mcmaster_part or line.designation
                if pn:
                    # Order quantity: packs when the part is sold in packs.
                    order = line.packs_needed(line.quantity * qty)
                    if order > 0:
                        f.write(f"{pn},{order}\n")


def descriptor_for_line(line, descriptor_registry: DescriptorRegistry, chassis: str) -> str:
    """Pick or prompt for a human-readable descriptor for a BOM line."""
    cat = line.type.lower()
    desc = line.description

    # PCBs: prompt with an abbreviation of the board name.
    if cat == "pcb":
        default = DescriptorRegistry.default_for_board(desc)
        return descriptor_registry.get_or_prompt(
            chassis, cat, desc, default=default, reason="PCB board descriptor"
        )

    # Wiring harnesses: prompt with the source/harness name.
    if cat == "wiring":
        if line.vendor_hint == "assembly":
            # Harness assembly line. A folder named like an IPN contributes its
            # descriptor with no prompt — with a rev (HW-C2-WH-GD-A) or
            # rev-less (HW-C2-WH-GD). The registry owns the live revision.
            src = (line.sources[0].split("/")[-1] if line.sources else desc).upper()
            m = re.match(r"^HW-[A-Z0-9]+-WH-([A-Z0-9\-_]+)-[A-Z]$", src)
            if not m:
                m = re.match(r"^HW-[A-Z0-9]+-WH-([A-Z0-9\-_]+)$", src)
            if m:
                descriptor = m.group(1)
                descriptor_registry.set(chassis, cat, desc, descriptor)
                descriptor_registry.save()
                return descriptor
            default = DescriptorRegistry.default_for_harness(src)
            return descriptor_registry.get_or_prompt(
                chassis, cat, desc, default=default, reason="wiring harness descriptor"
            )
        # Bulk wire/cable component inside a harness: auto-derive, no prompt.
        return DescriptorRegistry.default_for_commodity(cat, desc, line.footprint, line.designation)

    # Fabricated parts: use the clean part name, stripping any leading
    # HW-C2- prefix and trailing -A revision that may be in the folder name.
    if cat in ("busbar", "plate", "bracket", "3dprint"):
        clean = desc.upper()
        clean = re.sub(r"^HW-[A-Z0-9]+-", "", clean)
        clean = re.sub(r"-[A-Z]$", "", clean)
        return clean[:24] or re.sub(r"[^A-Z0-9\-_]+", "-", desc.upper()).strip("-")[:24]

    # Mechanical / fasteners: use the McMaster PN or slug.
    if cat in ("mechanical", "fastener"):
        return DescriptorRegistry.default_for_mechanical(desc)

    # Commodity components: auto-derive from value/footprint/MPN.
    if cat in ("resistor", "capacitor", "ic", "chip", "connector"):
        return DescriptorRegistry.default_for_commodity(cat, desc, line.footprint, line.designation)

    # Fallback: slugify description.
    return re.sub(r"[^A-Z0-9\-_]+", "-", desc.upper()).strip("-")[:24]


def zip_pcb_fab(fab_dir: Path, zip_path: Path) -> None:
    """Zip the contents of a KiCad Fab directory."""
    zip_path.parent.mkdir(parents=True, exist_ok=True)
    with zipfile.ZipFile(zip_path, "w", zipfile.ZIP_DEFLATED) as zf:
        for path in sorted(fab_dir.iterdir()):
            if path.is_file() and not path.name.startswith("."):
                zf.write(path, path.name)


# Spares tiers for --variants. Bare minimum (this run's outputs) orders exactly
# what the design needs; the two variants add spares by part class:
# - cheap parts (connectors, crimps, passives): percentage + minimum extra
# - medium parts ($1..$30: MCUs, gate drivers, gate-drive supplies): +N each
# - expensive parts (>$30: IGBTs etc.) and in-house assemblies: no spares
SPARE_TIERS = {
    "standard": {
        "cheap_max": 1.0, "pct": 0.25, "min_extra": 2,
        "medium_max": 30.0, "medium_extra": 0,
    },
    "generous": {
        "cheap_max": 1.0, "pct": 0.50, "min_extra": 5,
        "medium_max": 30.0, "medium_extra": 1,
    },
}


def spare_qty(line, unit_price, tier: dict) -> int:
    """Spare-adjusted quantity for a line under a tier policy."""
    if line.primary_vendor() == "assembly":
        return line.quantity
    if unit_price is not None and unit_price > tier["medium_max"]:
        return line.quantity
    per_piece = unit_price
    if per_piece is not None and line.pack_size and line.pack_size > 1:
        per_piece = per_piece / line.pack_size
    if per_piece is None or per_piece <= tier["cheap_max"]:
        import math
        return max(line.quantity + tier["min_extra"],
                   math.ceil(line.quantity * (1 + tier["pct"])))
    return line.quantity + tier["medium_extra"]


def write_gitignore(output_dir: Path, comparison: bool = False) -> None:
    """Write a .gitignore that keeps only the price report and this file."""
    gitignore = output_dir / ".gitignore"
    content = "# Generated by BOMManager. Only Pricing_Report.md is tracked.\n*\n!.gitignore\n!Pricing_Report.md\n"
    if comparison:
        content += "!Variant_Comparison.md\n!variants.json\n"
    gitignore.write_text(content, encoding="utf-8")


def write_variant_outputs(chassis: str, priced, base_output: Path, qty: int = 1) -> None:
    """Write spares-variant BOMs (standard/generous tiers) under Variants/.

    Vendor CSVs and the McMaster paste file use spare-adjusted quantities with
    the same pack math as the main outputs; a comparison summary is printed.
    """
    import dataclasses

    def tier_total(lines_prices):
        return sum(line_total(l, p) for l, p in lines_prices)

    base_total = tier_total([(e["line"], e["price"].unit_price) for e in priced]) * qty
    print(f"\n  Spares variants ({qty} unit{'s' if qty != 1 else ''}): bare minimum ${base_total:,.2f}")

    for tier_name, tier in SPARE_TIERS.items():
        out_dir = base_output / "BOMs" / "Variants" / tier_name
        out_dir.mkdir(parents=True, exist_ok=True)

        tier_lines = []
        total = 0.0
        for e in priced:
            line = e["line"]
            spare = spare_qty(line, e["price"].unit_price, tier)
            new_line = dataclasses.replace(line, quantity=spare * qty)
            tier_lines.append((new_line, e["price"].unit_price))
            total += line_total(new_line, e["price"].unit_price)

        lines = [l for l, _ in tier_lines]
        vendors_present = set(l.primary_vendor() or "unknown" for l in lines)
        for vendor in sorted(vendors_present):
            write_vendor_csv(lines, vendor, out_dir / f"{vendor}_bom.csv", qty=1)
        if "mcmaster" in vendors_present:
            write_mcmaster_order_paste(lines, out_dir / "McMaster_Order_Paste.txt", qty=1)

        with open(out_dir / "Consolidated_BOM.csv", "w", encoding="utf-8", newline="") as f:
            writer = csv.writer(f)
            writer.writerow(["Quantity", "Order Qty", "Pack Size", "Leftover", "Internal P/N",
                             "Description", "Customer Part No.", "Manufacturer Part No.",
                             "Vendor", "Vendor P/N", "Unit Price", "Line Total"])
            for line, price in tier_lines:
                vendor = line.primary_vendor() or "unknown"
                need = line.quantity
                packed = line.pack_size and line.pack_size > 1
                writer.writerow([
                    need,
                    line.packs_needed(need),
                    line.pack_size if packed else "",
                    line.leftover(need) if (packed or line.on_hand) else "",
                    line.internal_pn,
                    line.description,
                    line.customer_part,
                    line.designation,
                    vendor,
                    line.vendor_part_number(vendor),
                    f"{price:,.4f}" if price is not None else "",
                    f"{line_total(line, price):,.2f}" if price is not None else "",
                ])
        print(f"  {tier_name:<10} ${total:,.2f} (+${total - base_total:,.2f})  -> {out_dir.relative_to(base_output)}")


def write_variant_comparison(chassis: str, results: list, output_dir: Path, qty: int = 1) -> None:
    """Write Variant_Comparison.md and variants.json at the FabricationData root.

    `results` holds one entry per generated build variant: name, description,
    is_default, output dir, aggregated lines, priced lines, and applied changes.
    """
    default = next(r for r in results if r["is_default"])

    def total_of(r):
        return sum(line_total(e["line"], e["price"].unit_price) for e in r["priced"]) * qty

    def by_designation(r):
        return {e["line"].designation.upper(): (e["line"], e["price"].unit_price) for e in r["priced"]}

    default_lines = by_designation(default)

    md = [f"# Build Variant Comparison — {chassis}", "",
          "Generated by BOMManager from `variants.yaml`. The default variant's outputs "
          "live at the `FabricationData/` root; other builds are under `FabricationData/Builds/<variant>/`.",
          "", f"Totals at qty {qty}:", "",
          "| Variant | Description | BOM lines | Total |", "|---|---|---:|---:|"]
    for r in results:
        md.append(f"| {r['name']}{' (default)' if r['is_default'] else ''} | {r['description']} "
                  f"| {len(r['lines'])} | ${total_of(r):,.2f} |")

    manifest_variants = []
    for r in results:
        if r["is_default"]:
            added, removed, qty_diffs = [], [], []
        else:
            theirs = by_designation(r)
            added = sorted(d for d in theirs if d not in default_lines)
            removed = sorted(d for d in default_lines if d not in theirs)
            qty_diffs = sorted(d for d in theirs.keys() & default_lines.keys()
                               if theirs[d][0].quantity != default_lines[d][0].quantity)

            def section(title, designations, source):
                if not designations:
                    return
                md.extend(["", f"## {title}", "",
                           "| Qty | Designation | Description | Vendor P/N | Unit Price | Line Total |",
                           "|---:|---|---|---|---:|---:|"])
                for d in designations:
                    line, price = source[d]
                    md.append(f"| {line.quantity * qty} | {line.designation} | {line.description} "
                              f"| {line.vendor_part_number(line.primary_vendor()) or '—'} "
                              f"| {f'${price:,.4f}' if price is not None else '—'} "
                              f"| {f'${line_total(line, price, qty):,.2f}' if price is not None else '—'} |")

            section(f"Only in {r['name']}", added, theirs)
            section(f"Only in {default['name']} (removed in {r['name']})", removed, default_lines)
            if qty_diffs:
                md.extend(["", f"## Quantity differences ({default['name']} -> {r['name']})", "",
                           "| Designation | " + default['name'] + " qty | " + r['name'] + " qty |",
                           "|---|---:|---:|"])
                for d in qty_diffs:
                    md.append(f"| {theirs[d][0].designation} | {default_lines[d][0].quantity * qty} "
                              f"| {theirs[d][0].quantity * qty} |")

        manifest_variants.append({
            "name": r["name"],
            "description": r["description"],
            "is_default": r["is_default"],
            "output_dir": r["output_dir"],
            "bom_lines": len(r["lines"]),
            "total_usd": round(total_of(r), 2),
            "added_designations": added,
            "removed_designations": removed,
        })

    comparison_path = output_dir / "Variant_Comparison.md"
    comparison_path.write_text("\n".join(md) + "\n", encoding="utf-8")
    print(f"  Wrote Variant_Comparison.md")

    manifest = {
        "chassis": chassis,
        "default": default["name"],
        "qty": qty,
        "variants": manifest_variants,
    }
    with open(output_dir / "variants.json", "w", encoding="utf-8") as f:
        json.dump(manifest, f, indent=2)
        f.write("\n")
    print(f"  Wrote variants.json")


def write_chassis_outputs(
    chassis: str,
    lines,
    priced,
    boards: set,
    args: argparse.Namespace,
    output_dir: Path,
    hardware_root: Path,
    extra_report_sections=None,
    pcb_zips: bool = True,
    track_comparison: bool = False,
) -> None:
    """Write all output files for a single chassis."""
    output_dir.mkdir(parents=True, exist_ok=True)
    write_gitignore(output_dir, comparison=track_comparison)
    boms_dir = output_dir / "BOMs"
    boms_dir.mkdir(parents=True, exist_ok=True)

    selected_vendors = set(v.strip().lower() for v in args.vendors.split(","))
    if "all" not in selected_vendors:
        priced = [p for p in priced if (p["line"].primary_vendor() or "unknown") in selected_vendors]
        lines = [p["line"] for p in priced]

    vendors_present = set(p["line"].primary_vendor() or "unknown" for p in priced)
    written_csvs = set()
    for vendor in sorted(vendors_present):
        fname = f"{vendor}_bom.csv"
        write_vendor_csv(lines, vendor, boms_dir / fname, qty=args.qty)
        written_csvs.add(fname)
        print(f"  Wrote BOMs/{fname}")

    # Full runs own the BOMs directory: drop vendor CSVs whose source vanished
    # (e.g. a removed harness) so outputs never show stale state.
    if "all" in selected_vendors:
        for stale in boms_dir.glob("*_bom.csv"):
            if stale.name not in written_csvs and stale.name != "Consolidated_BOM.csv":
                stale.unlink()
                print(f"  Removed stale BOMs/{stale.name}")

    if "mcmaster" in vendors_present:
        paste_path = output_dir / "McMaster_Order_Paste.txt"
        write_mcmaster_order_paste(lines, paste_path, qty=args.qty)
        print(f"  Wrote McMaster_Order_Paste.txt")

    consolidated_path = boms_dir / "Consolidated_BOM.csv"
    with open(consolidated_path, "w", encoding="utf-8", newline="") as f:
        writer = csv.writer(f)
        writer.writerow(["Quantity", "Order Qty", "Pack Size", "Leftover", "Internal P/N", "Description", "Customer Part No.", "Manufacturer Part No.", "Vendor", "Vendor P/N"])
        for line in lines:
            vendor = line.primary_vendor() or "unknown"
            need = line.quantity * args.qty
            packed = line.pack_size and line.pack_size > 1
            writer.writerow([
                need,
                line.packs_needed(need),
                line.pack_size if packed else "",
                line.leftover(need) if (packed or line.on_hand) else "",
                line.internal_pn,
                line.description,
                line.customer_part,
                line.designation,
                vendor,
                line.vendor_part_number(vendor),
            ])
    print(f"  Wrote BOMs/Consolidated_BOM.csv")

    extra_qtys = [int(x) for x in args.extra_qtys.split(",") if x.strip().isdigit()]
    report_path = output_dir / "Pricing_Report.md"
    generate_markdown_report(priced, report_path, qty=args.qty, extra_qtys=extra_qtys, extra_sections=extra_report_sections)
    print(f"  Wrote Pricing_Report.md")

    if pcb_zips and not args.no_pcb_zips and boards:
        pcb_zip_dir = output_dir / "PCB_Fab_Zips"
        pcb_zip_dir.mkdir(parents=True, exist_ok=True)
        for board in sorted(boards):
            fab_dir = hardware_root / chassis / "Boards" / board / "Fab"
            if fab_dir.is_dir() and any(f.is_file() and not f.name.startswith(".") for f in fab_dir.iterdir()):
                zip_path = pcb_zip_dir / f"{board}.zip"
                zip_pcb_fab(fab_dir, zip_path)
                print(f"  Wrote PCB_Fab_Zips/{board}.zip")


def run(argv, ctx: Context) -> int:
    args = parse_args(argv)

    hardware_root = args.hardware_root or ctx.hardware_root
    config = ctx.config
    db = ctx.db
    cache = ctx.cache

    enabled = config.api_enabled_vendors()
    if ctx.mcmaster and ctx.mcmaster.enabled():
        enabled.append("mcmaster")
    if enabled:
        print(f"API-enabled vendors: {', '.join(sorted(set(enabled)))}")
    else:
        print("No vendor API keys configured; using cached/manual prices only.")
        print("See docs/API_KEYS.md for setup instructions.")

    # Discover and parse
    chassis_filter = args.chassis.split(",") if args.chassis else None
    board_filter = args.board.split(",") if args.board else None
    num_sources, items_by_chassis, boards_by_chassis, _ = collect_items(ctx, chassis_filter, board_filter, hardware_root=hardware_root)
    print(f"Discovered {num_sources} BOM sources.")

    if args.suggest:
        all_items = [item for items in items_by_chassis.values() for item in items]
        interactive_suggest(all_items, db, ctx.mouser, ctx.digikey, ctx.octopart)

    # Pricing engine
    engine = PricingEngine(
        cache=cache,
        mouser=ctx.mouser,
        digikey=ctx.digikey,
        octopart=ctx.octopart,
        mcmaster=ctx.mcmaster,
    )

    # Descriptor and part number registries
    pn_registry = ctx.pn_registry
    descriptor_registry = ctx.descriptor_registry
    descriptor_registry.allow_prompt = not args.no_prompt

    # Validate the build-variant selection up front so a typo aborts before
    # any outputs are written. selections[chassis] = (variant_set, [names]).
    selections = {}
    for chassis in sorted(items_by_chassis):
        if chassis_filter and chassis not in chassis_filter:
            continue
        variant_set = load_chassis_variants(hardware_root / chassis)
        if args.variant:
            names = [v.strip() for v in args.variant.split(",") if v.strip()]
            if variant_set is None:
                names = [None]  # noted below; base build only
            else:
                for name in names:
                    try:
                        variant_set.get(name)
                    except ValueError as e:
                        print(f"Error: {e}")
                        return 1
        elif variant_set is not None:
            names = variant_set.names()
        else:
            names = [None]
        selections[chassis] = (variant_set, names)

    for chassis in sorted(items_by_chassis):
        if chassis_filter and chassis not in chassis_filter:
            continue

        chassis_abbr = DescriptorRegistry.abbreviate_chassis(chassis)
        print(f"\n=== {chassis} ({chassis_abbr}) ===")
        base_items = items_by_chassis[chassis]
        variant_set, selected = selections[chassis]

        if args.variant and variant_set is None:
            print(f"  (no variants.yaml for {chassis}; ignoring --variant, base build only)")

        # Determine output directory
        if args.output_dir:
            chassis_output = args.output_dir
        else:
            chassis_output = hardware_root / chassis / "FabricationData"

        results = []
        for name in selected:
            variant = variant_set.get(name) if variant_set is not None else None
            is_default = variant_set is None or name == variant_set.default
            items = base_items
            changes = []
            if variant is not None:
                label = f"{name} (default)" if is_default else f"{name} -> Builds/{name}/"
                print(f"\n  --- build variant: {label} ---")
                items, changes = apply_variant(list(base_items), variant,
                                               chassis_dir=hardware_root / chassis)

            # Aggregate
            lines = aggregate_bom(iter(items), db, spares=args.spares, extra_pct=args.spares_pct, inventory=ctx.inventory)
            print(f"Consolidated to {len(lines)} unique BOM lines.")

            # Assign internal part numbers
            for line in lines:
                if not line.internal_pn:
                    descriptor = descriptor_for_line(line, descriptor_registry, chassis)
                    line.internal_pn = pn_registry.generate_pn(
                        line.type, line.description, descriptor=descriptor,
                        chassis=chassis_abbr, identity=line_identity(line),
                    )

            # Price
            priced = engine.price_lines(lines, refresh=args.refresh_prices)

            out_dir = chassis_output if is_default else chassis_output / "Builds" / name
            try:
                rel_out = str(out_dir.relative_to(hardware_root / chassis))
            except ValueError:
                rel_out = str(out_dir)  # --output-dir outside the chassis tree
            write_chassis_outputs(
                chassis=chassis,
                lines=lines,
                priced=priced,
                boards=boards_by_chassis[chassis],
                args=args,
                output_dir=out_dir,
                hardware_root=hardware_root,
                extra_report_sections=fab.render_markdown(fab.collect(ctx, chassis, hardware_root)),
                pcb_zips=is_default,
                track_comparison=variant_set is not None and is_default,
            )

            # Spares tiers stay at the root output (default build) only.
            if args.variants and is_default:
                write_variant_outputs(chassis, priced, out_dir, qty=args.qty)

            results.append({
                "name": name,
                "description": variant.description if variant else "",
                "is_default": is_default,
                "output_dir": rel_out,
                "lines": lines,
                "priced": priced,
                "changes": changes,
            })

        if variant_set is not None and len(results) > 1 and any(r["is_default"] for r in results):
            write_variant_comparison(chassis, results, chassis_output, qty=args.qty)

    pn_registry.save()
    cache.save()
    print("\nDone.")
    return 0


def main() -> int:
    """Standalone entry (kept for the deprecated generate_bom.py shim)."""
    from .context import build_context

    return run(sys.argv[1:], build_context())
