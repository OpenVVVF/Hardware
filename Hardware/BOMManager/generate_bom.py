#!/usr/bin/env python3
"""Generate consolidated BOMs, price reports, and PCB fab zip bundles."""

import argparse
import csv
import re
import sys
import zipfile
from collections import defaultdict
from pathlib import Path

from bom_manager.bom import aggregate_bom
from bom_manager.config import Config
from bom_manager.db import PartDatabase
from bom_manager.descriptor_registry import DescriptorRegistry
from bom_manager.discover import discover_boms
from bom_manager.parsers import LineItem, parse_source
from bom_manager.part_numbers import PartNumberRegistry
from bom_manager.pricing import PriceCache, PricingEngine, generate_markdown_report
from bom_manager.suggest import interactive_suggest
from bom_manager.vendors import (
    DigiKeyClient,
    McMasterClient,
    MouserClient,
    OctopartClient,
    SendCutSendClient,
)


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description="Generate consolidated hardware BOMs.")
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
        "--no-pcb-zips",
        action="store_true",
        help="Skip creating PCB fab zip bundles",
    )
    parser.add_argument(
        "--no-prompt",
        action="store_true",
        help="Error if a descriptor is missing instead of prompting (useful for CI)",
    )
    return parser.parse_args()


def write_vendor_csv(lines, vendor: str, path: Path) -> None:
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
                    writer.writerow([
                        line.vendor_part_number(vendor),
                        line.quantity,
                        line.designation,
                        line.internal_pn,
                        line.description,
                    ])
        elif vendor == "digikey":
            writer.writerow(["Quantity", "Digi-Key Part Number", "Manufacturer Part Number", "Description", "Customer Reference"])
            for line in lines:
                if line.primary_vendor() == vendor:
                    writer.writerow([
                        line.quantity,
                        line.vendor_part_number(vendor),
                        line.designation,
                        line.description,
                        line.internal_pn,
                    ])
        else:
            writer.writerow(["Quantity", "Internal P/N", "Description", "Customer Part No.", "Manufacturer Part No.", "Vendor", "Vendor P/N"])
            for line in lines:
                if line.primary_vendor() == vendor:
                    writer.writerow([
                        line.quantity,
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
                    f.write(f"{pn},{line.quantity * qty}\n")


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
        # Prefer the source label if it looks like a harness name
        src = (line.sources[0].split("/")[-1] if line.sources else desc).upper()
        default = DescriptorRegistry.default_for_harness(src)
        return descriptor_registry.get_or_prompt(
            chassis, cat, desc, default=default, reason="wiring harness descriptor"
        )

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


def write_gitignore(output_dir: Path) -> None:
    """Write a .gitignore that keeps only the price report and this file."""
    gitignore = output_dir / ".gitignore"
    content = "# Generated by BOMManager. Only Pricing_Report.md is tracked.\n*\n!.gitignore\n!Pricing_Report.md\n"
    gitignore.write_text(content, encoding="utf-8")


def write_chassis_outputs(
    chassis: str,
    lines,
    priced,
    boards: set,
    args: argparse.Namespace,
    output_dir: Path,
    hardware_root: Path,
) -> None:
    """Write all output files for a single chassis."""
    output_dir.mkdir(parents=True, exist_ok=True)
    write_gitignore(output_dir)
    boms_dir = output_dir / "BOMs"
    boms_dir.mkdir(parents=True, exist_ok=True)

    selected_vendors = set(v.strip().lower() for v in args.vendors.split(","))
    if "all" not in selected_vendors:
        priced = [p for p in priced if (p["line"].primary_vendor() or "unknown") in selected_vendors]
        lines = [p["line"] for p in priced]

    vendors_present = set(p["line"].primary_vendor() or "unknown" for p in priced)
    for vendor in sorted(vendors_present):
        write_vendor_csv(lines, vendor, boms_dir / f"{vendor}_bom.csv")
        print(f"  Wrote BOMs/{vendor}_bom.csv")

    if "mcmaster" in vendors_present:
        paste_path = output_dir / "McMaster_Order_Paste.txt"
        write_mcmaster_order_paste(lines, paste_path, qty=args.qty)
        print(f"  Wrote McMaster_Order_Paste.txt")

    consolidated_path = boms_dir / "Consolidated_BOM.csv"
    with open(consolidated_path, "w", encoding="utf-8", newline="") as f:
        writer = csv.writer(f)
        writer.writerow(["Quantity", "Internal P/N", "Description", "Customer Part No.", "Manufacturer Part No.", "Vendor", "Vendor P/N"])
        for line in lines:
            vendor = line.primary_vendor() or "unknown"
            writer.writerow([
                line.quantity * args.qty,
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
    generate_markdown_report(priced, report_path, qty=args.qty, extra_qtys=extra_qtys)
    print(f"  Wrote Pricing_Report.md")

    if not args.no_pcb_zips and boards:
        pcb_zip_dir = output_dir / "PCB_Fab_Zips"
        pcb_zip_dir.mkdir(parents=True, exist_ok=True)
        for board in sorted(boards):
            fab_dir = hardware_root / chassis / "Boards" / board / "Fab"
            if fab_dir.is_dir():
                zip_path = pcb_zip_dir / f"{board}.zip"
                zip_pcb_fab(fab_dir, zip_path)
                print(f"  Wrote PCB_Fab_Zips/{board}.zip")


def main() -> int:
    args = parse_args()

    # Paths
    bom_manager_root = Path(__file__).resolve().parent
    hardware_root = args.hardware_root or bom_manager_root.parent

    # Config and database
    config = Config(bom_manager_root / "config.yaml")
    db = PartDatabase(bom_manager_root / "bom_manager" / "data" / "part_database.json")
    cache = PriceCache(bom_manager_root / "bom_manager" / "data" / "price_cache.json")

    # Vendor clients
    mouser = MouserClient(config.get("mouser.api_key"))
    digikey = DigiKeyClient(
        config.get("digikey.client_id"),
        config.get("digikey.client_secret"),
        sandbox=bool(config.get("digikey.sandbox", False)),
    )
    octopart = OctopartClient(config.get("octopart.api_key"))
    cert_path = config.get("mcmaster.cert_path")
    mcmaster = McMasterClient(
        username=config.get("mcmaster.username"),
        password=config.get("mcmaster.password"),
        cert_path=Path(cert_path) if cert_path else None,
        cert_password=config.get("mcmaster.cert_password") or None,
        use_scrape=True,
    )
    sendcutsend = SendCutSendClient(
        bom_manager_root / "bom_manager" / "data" / "sendcutsend_manifest.csv"
    )

    enabled = config.api_enabled_vendors()
    if mcmaster.enabled():
        enabled.append("mcmaster")
    if enabled:
        print(f"API-enabled vendors: {', '.join(sorted(set(enabled)))}")
    else:
        print("No vendor API keys configured; using cached/manual prices only.")
        print("See docs/API_KEYS.md for setup instructions.")

    # Discover and parse
    chassis_filter = args.chassis.split(",") if args.chassis else None
    board_filter = args.board.split(",") if args.board else None
    sources = list(discover_boms(hardware_root, chassis_filter, board_filter))
    print(f"Discovered {len(sources)} BOM sources.")

    items_by_chassis: dict = defaultdict(list)
    boards_by_chassis: dict = defaultdict(set)
    for src in sources:
        for item in parse_source(src):
            items_by_chassis[src.chassis].append(item)
            if src.category == "board":
                boards_by_chassis[src.chassis].add(src.board)

    # Add one PCB-fabrication line item per discovered board.
    for chassis, boards in boards_by_chassis.items():
        for board in boards:
            items_by_chassis[chassis].append(
                LineItem(
                    chassis=chassis,
                    source=board,
                    category="pcb_fab",
                    footprint="PCB",
                    designation=board,
                    quantity=1,
                    designators="",
                    vendor_hint="pcb",
                )
            )

    if args.suggest:
        all_items = [item for items in items_by_chassis.values() for item in items]
        interactive_suggest(all_items, db, mouser, digikey, octopart)
        db = PartDatabase(bom_manager_root / "bom_manager" / "data" / "part_database.json")

    # Pricing engine
    engine = PricingEngine(
        cache=cache,
        mouser=mouser,
        digikey=digikey,
        octopart=octopart,
        mcmaster=mcmaster,
        sendcutsend=sendcutsend,
    )

    # Descriptor and part number registries
    pn_format = config.get("part_number.format", PartNumberRegistry.DEFAULT_FORMAT)
    pn_registry = PartNumberRegistry(
        bom_manager_root / "bom_manager" / "data" / "part_numbers.json",
        format=pn_format,
    )
    descriptor_registry = DescriptorRegistry(
        bom_manager_root / "bom_manager" / "data" / "part_descriptors.json",
        allow_prompt=not args.no_prompt,
    )

    for chassis in sorted(items_by_chassis):
        if chassis_filter and chassis not in chassis_filter:
            continue

        chassis_abbr = DescriptorRegistry.abbreviate_chassis(chassis)
        print(f"\n=== {chassis} ({chassis_abbr}) ===")
        items = items_by_chassis[chassis]

        # Aggregate
        lines = aggregate_bom(iter(items), db, spares=args.spares, extra_pct=args.spares_pct)
        print(f"Consolidated to {len(lines)} unique BOM lines.")

        # Assign internal part numbers
        for line in lines:
            if not line.internal_pn:
                descriptor = descriptor_for_line(line, descriptor_registry, chassis)
                line.internal_pn = pn_registry.generate_pn(
                    line.type, line.description, descriptor=descriptor, chassis=chassis_abbr
                )

        # Price
        priced = engine.price_lines(lines, refresh=args.refresh_prices)

        # Determine output directory
        if args.output_dir:
            chassis_output = args.output_dir
        else:
            chassis_output = hardware_root / chassis / "FabricationData"

        write_chassis_outputs(
            chassis=chassis,
            lines=lines,
            priced=priced,
            boards=boards_by_chassis[chassis],
            args=args,
            output_dir=chassis_output,
            hardware_root=hardware_root,
        )

    pn_registry.save()
    cache.save()
    print("\nDone.")
    return 0


if __name__ == "__main__":
    sys.exit(main())
