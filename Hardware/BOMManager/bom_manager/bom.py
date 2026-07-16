"""BOM aggregation, substitution, and spares logic."""

import re
from dataclasses import dataclass, field
from pathlib import Path
from typing import Dict, Iterator, List, Optional

from .db import PartDatabase
from .parsers import LineItem


@dataclass
class BomLine:
    key: str
    footprint: str
    designation: str
    description: str
    customer_part: str
    quantity: int
    type: str
    sources: List[str] = field(default_factory=list)
    mouser_part: str = ""
    digikey_part: str = ""
    octopart_uid: str = ""
    mcmaster_part: str = ""
    sendcutsend_id: str = ""
    vendor_hint: Optional[str] = None
    manual_price: Optional[float] = None
    pack_size: int = 1   # units per order pack; 1 = sold individually
    on_hand: int = 0     # pieces already on your shelf (local inventory)
    image_path: Optional[Path] = None
    step_path: Optional[Path] = None
    metadata: Dict[str, str] = field(default_factory=dict)
    internal_pn: str = ""

    def packs_needed(self, need: int) -> int:
        """Order quantity (packs when pack_size > 1, else pieces) to cover `need`."""
        remaining = max(0, need - self.on_hand)
        if self.pack_size and self.pack_size > 1:
            return -(-remaining // self.pack_size)  # ceil
        return remaining

    def pieces_ordered(self, need: int) -> int:
        """How many physical pieces the order delivers for `need`."""
        packs = self.packs_needed(need)
        return packs * self.pack_size if self.pack_size and self.pack_size > 1 else packs

    def leftover(self, need: int) -> int:
        """Pieces left on the shelf after consuming `need`."""
        return self.on_hand + self.pieces_ordered(need) - need

    def primary_vendor(self) -> Optional[str]:
        if self.mouser_part and self.mouser_part != "DNO":
            return "mouser"
        if self.digikey_part and self.digikey_part != "DNO":
            return "digikey"
        if self.mcmaster_part:
            return "mcmaster"
        if self.sendcutsend_id:
            return "sendcutsend"
        if self.vendor_hint:
            return self.vendor_hint.lower()
        return None

    def vendor_part_number(self, vendor: Optional[str] = None) -> str:
        vendor = vendor or self.primary_vendor()
        # Vendor-list rows (MechanicalBOM.txt) carry the PN in the designation
        # and the vendor name in the footprint — use it when no explicit
        # vendor mapping exists in the part database.
        from_vendor_list = self.footprint.strip().lower() == (vendor or "").lower()
        fallback = self.designation if from_vendor_list else ""
        if vendor == "mouser":
            return self.mouser_part or fallback
        if vendor == "digikey":
            return self.digikey_part or fallback
        if vendor == "mcmaster":
            return self.mcmaster_part or self.designation
        if vendor == "sendcutsend":
            return self.sendcutsend_id or self.designation.split("|")[0].strip()
        if vendor == "pcb":
            return self.designation
        return fallback


def _component_type(footprint: str, designation: str, source: str = "", category: str = "") -> str:
    fp = footprint.lower()
    des = designation.lower()
    src = source.lower()
    cat = category.lower()

    # Explicit category hints from the parser/discoverer.
    if cat == "pcb_fab":
        return "pcb"
    if cat == "harness_asm":
        return "wiring"

    # PCB components from KiCad footprints
    if fp.startswith("r_"):
        return "resistor"
    if fp.startswith("c_"):
        return "capacitor"
    if any(x in footprint.upper() for x in ["HEADER", "SOCKET", "CONN", "PINHEADER", "RECEPTACLE"]):
        return "connector"
    # Use word-boundary-ish check for IC packages so "IDC" doesn't match "IC".
    fp_upper = footprint.upper()
    if any(re.search(rf"(^|[^A-Z]){re.escape(x)}([^A-Z]|$)", fp_upper) for x in ["IC", "SOIC", "QFP", "BGA", "LQFP", "SSOP"]):
        return "chip"

    # Fabricated / mechanical parts
    if "pcb" in des or footprint.lower().endswith(".kicad_pcb") or "board" in des:
        return "pcb"
    if "cable" in des:
        return "cable"
    if any(x in des for x in ["wire", "harness", "lead", "jumper"]):
        return "wiring"
    if "3d print" in des or "printed" in des:
        return "3dprint"

    # Bus bars, plates, brackets — check both description and source/folder name.
    busbar_tokens = ["bus bar", "busbar", "dclbb", "pbb", "bb", "dclb", "phasebar"]
    if any(x in des for x in busbar_tokens) or any(tok in src for tok in busbar_tokens):
        return "busbar"

    bracket_tokens = ["bracket", "brk", "mount"]
    if any(x in des for x in bracket_tokens) or any(tok in src for tok in bracket_tokens):
        return "bracket"

    plate_tokens = ["plate", "hsp", "heatsink", "spreader", "shield", "cover", "base plate"]
    if any(x in des for x in plate_tokens) or any(tok in src for tok in plate_tokens):
        return "plate"

    # Short abbreviations often used in fabricated-part folder names.
    src_norm = re.sub(r"[\-_]", "", src)
    if "dclbb" in src_norm or "pbb" in src_norm or "dclb" in src_norm:
        return "busbar"
    if "chsp" in src_norm or "hsp" in src_norm or "hs" in src_norm or "bsp" in src_norm:
        return "plate"
    if "brk" in src_norm or "bracket" in src_norm:
        return "bracket"

    if any(x in des for x in ["screw", "bolt", "nut", "washer", "standoff", "fastener"]):
        return "fastener"
    if footprint.lower() == "mcmaster":
        return "mechanical"
    return "other"


def _round_to_standard(qty: int) -> int:
    for val in [10, 25, 50, 100, 200, 500, 1000]:
        if qty <= val:
            return val
    # Round up to next 500
    return ((qty + 499) // 500) * 500


def _as_positive_int(value, default: int = 0) -> int:
    try:
        n = int(value)
    except (TypeError, ValueError):
        return default
    return n if n > 0 else default


def aggregate_bom(
    items: Iterator[LineItem],
    db: PartDatabase,
    spares: str = "none",
    extra_pct: float = 0.0,
    inventory=None,
) -> List[BomLine]:
    """Aggregate line items, apply substitutions, and apply spares policy."""
    raw: Dict[str, BomLine] = {}

    for item in items:
        key = db.normalize_key(item.footprint, item.designation)
        entry = db.lookup(item.footprint, item.designation)

        if entry and entry.get("mouser_part") == "DNO":
            continue

        if key not in raw:
            if entry:
                line = BomLine(
                    key=key,
                    footprint=item.footprint,
                    designation=item.designation,
                    description=entry.get("description", item.designation),
                    customer_part=entry.get("customer_part", ""),
                    quantity=0,
                    type=entry.get("type", _component_type(item.footprint, item.designation, item.source, item.category)),
                    mouser_part=entry.get("mouser_part", ""),
                    digikey_part=entry.get("digikey_part", ""),
                    octopart_uid=entry.get("octopart_uid", ""),
                    mcmaster_part=entry.get("mcmaster_part", ""),
                    sendcutsend_id=entry.get("sendcutsend_id", ""),
                    vendor_hint=item.vendor_hint,
                    manual_price=entry.get("manual_price"),
                    pack_size=_as_positive_int(entry.get("pack_size"), default=1),
                    on_hand=inventory.get(item.footprint, item.designation) if inventory else 0,
                    image_path=item.image_path,
                    step_path=item.step_path,
                    metadata=dict(item.metadata or {}),
                )
            else:
                line = BomLine(
                    key=key,
                    footprint=item.footprint,
                    designation=item.designation,
                    description=item.designation,
                    customer_part="",
                    quantity=0,
                    type=_component_type(item.footprint, item.designation, item.source, item.category),
                    vendor_hint=item.vendor_hint,
                    image_path=item.image_path,
                    step_path=item.step_path,
                    metadata=dict(item.metadata or {}),
                )
            raw[key] = line

        raw[key].quantity += item.quantity
        source_label = f"{item.chassis}/{item.source}"
        if source_label not in raw[key].sources:
            raw[key].sources.append(source_label)

    lines = list(raw.values())

    for line in lines:
        # Apply spares
        if spares == "cheap":
            if line.type in ("resistor", "capacitor") or (line.vendor_hint == "mcmaster"):
                line.quantity = max(line.quantity, _round_to_standard(line.quantity))
        elif spares == "all":
            line.quantity = int(line.quantity * (1 + extra_pct / 100.0))
            if line.quantity < 1:
                line.quantity = 1

    return sorted(lines, key=lambda x: (x.type, x.description.lower()))
