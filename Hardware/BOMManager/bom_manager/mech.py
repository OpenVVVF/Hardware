"""MechanicalBOM.txt management — the purchased-hardware list the tool owns.

The file stays a plain CSV (Qty,Vendor,PN,Description) so it remains readable
and diffable, but all edits go through this module so the format stays
canonical and rows stay deduplicated. Any vendor string is allowed
(McMaster, Digikey, Mitsubishi, ...).
"""

import csv
from dataclasses import dataclass
from pathlib import Path
from typing import List, Optional, Tuple

from .bom import _component_type
from .db import VENDOR_PN_FIELD, PartDatabase

HEADER = ["Qty", "Vendor", "PN", "Description"]


@dataclass
class MechRow:
    qty: int
    vendor: str
    pn: str
    description: str = ""


def _norm(text: str) -> str:
    return text.strip().lower()


def mech_file_path(hardware_root: Path, chassis: str) -> Path:
    return hardware_root / chassis / "Mechanical" / "MechanicalBOM.txt"


def load(path: Path) -> List[MechRow]:
    """Parse a MechanicalBOM file tolerantly (same shapes parse_simple_csv accepts)."""
    if not path.exists():
        return []
    rows: List[MechRow] = []
    with open(path, "r", encoding="utf-8-sig") as f:
        sample = f.read(4096)
        f.seek(0)
        delimiter = ","
        if ";" in sample and sample.count(";") > sample.count(","):
            delimiter = ";"
        reader = csv.DictReader(f, delimiter=delimiter)
        if reader.fieldnames:
            reader.fieldnames = [n.strip() for n in reader.fieldnames]
        for r in reader:
            pn = (r.get("PN") or r.get("PartNumber") or r.get("Part Number") or "").strip()
            vendor = (r.get("Vendor") or "").strip()
            if not pn or not vendor:
                continue
            try:
                qty = int(float((r.get("Qty") or r.get("Quantity") or "0").strip()))
            except (ValueError, AttributeError):
                continue
            if qty <= 0:
                continue
            rows.append(MechRow(qty, vendor, pn, (r.get("Description") or "").strip()))
    return rows


def save(path: Path, rows: List[MechRow]) -> None:
    """Write the canonical file: header + rows sorted by vendor, then PN."""
    path.parent.mkdir(parents=True, exist_ok=True)
    ordered = sorted(rows, key=lambda r: (_norm(r.vendor), _norm(r.pn)))
    with open(path, "w", encoding="utf-8", newline="") as f:
        writer = csv.writer(f)
        writer.writerow(HEADER)
        for r in ordered:
            writer.writerow([r.qty, r.vendor, r.pn, r.description])


def find(rows: List[MechRow], query: str) -> List[MechRow]:
    """Exact PN match first; otherwise substring match on PN or description."""
    q = _norm(query)
    exact = [r for r in rows if _norm(r.pn) == q]
    if exact:
        return exact
    return [r for r in rows if q in _norm(r.pn) or (r.description and q in _norm(r.description))]


def upsert(
    rows: List[MechRow], vendor: str, pn: str, qty: int, description: str = ""
) -> Tuple[MechRow, bool]:
    """Add or update a row (identity = vendor + PN). Returns (row, created)."""
    for r in rows:
        if _norm(r.vendor) == _norm(vendor) and _norm(r.pn) == _norm(pn):
            r.qty = qty
            if description:
                r.description = description
            return r, False
    row = MechRow(qty, vendor.strip(), pn.strip(), description.strip())
    rows.append(row)
    return row, True


def set_qty(rows: List[MechRow], pn: str, qty: int) -> Optional[MechRow]:
    matches = find(rows, pn)
    if len(matches) != 1:
        return None
    matches[0].qty = qty
    return matches[0]


def remove(rows: List[MechRow], pn: str) -> Optional[MechRow]:
    matches = find(rows, pn)
    if len(matches) != 1:
        return None
    rows.remove(matches[0])
    return matches[0]


def ensure_db_entry(db: PartDatabase, row: MechRow) -> bool:
    """Create a part-database entry for the row if none exists, so prices and
    pack sizes have somewhere to attach. Returns True when created.

    The description stays equal to the PN on purpose: internal-PN registry
    keys derive from the description, so a friendly description here would
    silently renumber existing parts. (Descriptions become safe to edit once
    registry keys are identity-based.)
    """
    if db.lookup(row.vendor, row.pn):
        return False
    vendor_field = VENDOR_PN_FIELD.get(_norm(row.vendor), "")
    entry = {
        "description": row.pn,
        "customer_part": "",
        "type": _component_type(row.vendor, f"{row.pn} {row.description}".strip()),
        "mouser_part": "",
        "digikey_part": "",
        "octopart_uid": "",
        "mcmaster_part": "",
        "sendcutsend_id": "",
        "manual_price": None,
        "price_currency": "USD",
        "price_updated": None,
        "notes": "",
    }
    if vendor_field:
        entry[vendor_field] = row.pn
    db.add(row.vendor, row.pn, entry)
    return True


def row_price(ctx, row: MechRow):
    """Best known price for a row: manual DB price first, then the cache.
    Returns (unit_price, source, pack_size)."""
    pack_size = 1
    entry = ctx.db.lookup(row.vendor, row.pn)
    if entry:
        pack_size = entry.get("pack_size") or 1
        if entry.get("manual_price") is not None:
            return entry["manual_price"], "manual", pack_size
    info = ctx.cache.get(row.vendor.lower(), row.pn)
    if info and info.unit_price is not None:
        return info.unit_price, info.source, pack_size
    return None, "", pack_size
