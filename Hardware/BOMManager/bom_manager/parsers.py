"""Parse BOM source files into normalized line items."""

import csv
import re
from dataclasses import dataclass
from pathlib import Path
from typing import Dict, Iterator, List, Optional


@dataclass
class LineItem:
    chassis: str
    source: str      # board name or source stem
    category: str    # 'board', 'mechanical', 'custom', 'sendcutsend_folder'
    footprint: str
    designation: str
    quantity: int
    designators: str = ""
    vendor_hint: Optional[str] = None
    image_path: Optional[Path] = None
    step_path: Optional[Path] = None
    metadata: dict = None

    def __post_init__(self):
        if self.metadata is None:
            self.metadata = {}


def _normalize(text: str) -> str:
    return re.sub(r"\s+", "", text.strip().lower())


def _strip_fieldnames(reader: csv.DictReader) -> csv.DictReader:
    """Return a new DictReader with stripped header fieldnames."""
    if reader.fieldnames is None:
        return reader
    reader.fieldnames = [name.strip() for name in reader.fieldnames]
    return reader


def parse_kicad_fab_csv(path: Path, chassis: str, board: str, category: str = "board") -> Iterator[LineItem]:
    """Parse a KiCad BOM export CSV (board Fab output or schematic-only harness export)."""
    with open(path, "r", encoding="utf-8-sig") as f:
        sample = f.read(4096)
        f.seek(0)
        dialect = csv.Sniffer().sniff(sample, delimiters=";,\t")
        reader = _strip_fieldnames(csv.DictReader(f, delimiter=dialect.delimiter))
        for row in reader:
            try:
                qty = int(row.get("Quantity", "0").strip())
            except ValueError:
                continue
            if qty <= 0:
                continue
            yield LineItem(
                chassis=chassis,
                source=board,
                category=category,
                footprint=row.get("Footprint", "").strip(),
                designation=row.get("Designation", "").strip(),
                quantity=qty,
                designators=row.get("Designator", "").strip(),
                vendor_hint="mouser",
            )


def parse_simple_csv(path: Path, chassis: str, category: str, source: str,
                     vendor_hint: Optional[str] = None) -> Iterator[LineItem]:
    """Parse Qty,Vendor,PN style CSV/TXT files (mechanical BOMs)."""
    with open(path, "r", encoding="utf-8-sig") as f:
        sample = f.read(4096)
        f.seek(0)
        delimiter = ","
        if ";" in sample and sample.count(";") > sample.count(","):
            delimiter = ";"
        reader = _strip_fieldnames(csv.DictReader(f, delimiter=delimiter))
        for row in reader:
            try:
                qty = int(float(row.get("Qty", row.get("Quantity", "0")).strip()))
            except (ValueError, AttributeError):
                continue
            if qty <= 0:
                continue
            pn = row.get("PN", row.get("PartNumber", row.get("Part Number", ""))).strip()
            vendor = row.get("Vendor", "").strip()
            description = row.get("Description", "").strip()
            designation = pn or description or source
            effective_vendor = (vendor.lower() if vendor else None) or vendor_hint
            yield LineItem(
                chassis=chassis,
                source=source,
                category=category,
                footprint=vendor or vendor_hint or "",
                designation=designation,
                quantity=qty,
                designators="",
                vendor_hint=effective_vendor,
            )


def parse_custom_parts_csv(path: Path, chassis: str, source: str,
                           vendor_hint: Optional[str] = "sendcutsend") -> Iterator[LineItem]:
    """Parse custom parts CSV. Expected columns include PartName, Material, Thickness_mm,
    Qty, UnitPrice, Dimensions_mm, Finish, Notes."""
    with open(path, "r", encoding="utf-8-sig") as f:
        sample = f.read(4096)
        f.seek(0)
        delimiter = ","
        if ";" in sample and sample.count(";") > sample.count(","):
            delimiter = ";"
        reader = _strip_fieldnames(csv.DictReader(f, delimiter=delimiter))
        for row in reader:
            try:
                qty = int(float(row.get("Qty", row.get("Quantity", "0")).strip()))
            except (ValueError, AttributeError):
                continue
            if qty <= 0:
                continue
            part = row.get("PartName", row.get("Part", "")).strip()
            material = row.get("Material", "").strip()
            thickness = row.get("Thickness_mm", "").strip()
            finish = row.get("Finish", "").strip()
            notes = row.get("Notes", "").strip()
            dims = row.get("Dimensions_mm", "").strip()
            designation = " | ".join(filter(None, [part, material, thickness, finish, dims, notes]))
            yield LineItem(
                chassis=chassis,
                source=source,
                category="custom",
                footprint="",
                designation=designation,
                quantity=qty,
                designators="",
                vendor_hint=vendor_hint,
            )


def parse_sendcutsend_folder(path: Path, chassis: str, part_name: str) -> Iterator[LineItem]:
    """Parse a fabricated part folder with info.txt, STEP, and optional image."""
    info_file = path / "info.txt"
    metadata: Dict[str, str] = {}
    if info_file.exists():
        with open(info_file, "r", encoding="utf-8") as f:
            for line in f:
                line = line.strip()
                if not line or line.startswith("#"):
                    continue
                if "=" in line:
                    key, val = line.split("=", 1)
                    metadata[key.strip()] = val.strip()
    else:
        metadata["Notes"] = "info.txt missing — run import_sendcutsend_cart.py to populate"

    try:
        qty = int(float(metadata.get("Qty", "1").strip()))
    except (ValueError, AttributeError):
        qty = 1

    part = metadata.get("PartName", part_name).strip() or part_name
    material = metadata.get("Material", "").strip()
    thickness = metadata.get("Thickness_mm", "").strip()
    finish = metadata.get("Finish", "").strip()
    notes = metadata.get("Notes", "").strip()
    dims = metadata.get("Dimensions_mm", metadata.get("Dimensions_in", "")).strip()

    # Keep the full spec in metadata for reporting; use the clean PartName as the
    # designation so BOM lines and internal part numbers are readable.
    metadata["FullSpec"] = " | ".join(filter(None, [material, thickness, finish, dims, notes]))
    designation = part

    step_file = path / f"{part_name}.step"
    if not step_file.exists():
        step_files = list(path.glob("*.step")) + list(path.glob("*.STEP")) + list(path.glob("*.stp"))
        if step_files:
            step_file = step_files[0]
        else:
            step_file = None

    image_file = path / "info.png"
    if not image_file.exists():
        image_files = list(path.glob("*.png")) + list(path.glob("*.jpg")) + list(path.glob("*.jpeg"))
        if image_files:
            image_file = image_files[0]
        else:
            image_file = None

    yield LineItem(
        chassis=chassis,
        source=part_name,
        category="sendcutsend_folder",
        footprint="",
        designation=designation,
        quantity=qty,
        designators="",
        vendor_hint="sendcutsend",
        image_path=image_file,
        step_path=step_file,
        metadata=metadata,
    )


def parse_source(source) -> Iterator[LineItem]:
    """Route a discovered BomSource to the correct parser."""
    if source.category == "board":
        yield from parse_kicad_fab_csv(source.path, source.chassis, source.board)
    elif source.category == "harness":
        yield from parse_kicad_fab_csv(source.path, source.chassis, source.board, category="harness")
    elif source.category == "mechanical":
        yield from parse_simple_csv(source.path, source.chassis, source.category, source.board, source.vendor_hint)
    elif source.category == "custom":
        yield from parse_custom_parts_csv(source.path, source.chassis, source.board, source.vendor_hint)
    elif source.category == "sendcutsend_folder":
        yield from parse_sendcutsend_folder(source.path, source.chassis, source.board)
