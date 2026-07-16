"""Part database: substitutions, vendor mappings, exclusions."""

import json
import re
from pathlib import Path
from typing import Any, Dict, Optional

# Vendor string -> part database entry field holding that vendor's part number.
VENDOR_PN_FIELD = {
    "mouser": "mouser_part",
    "digikey": "digikey_part",
    "mcmaster": "mcmaster_part",
    "sendcutsend": "sendcutsend_id",
}


class PartDatabase:
    def __init__(self, db_path: Optional[Path] = None):
        if db_path is None:
            self.db_path = Path(__file__).resolve().parent / "data" / "part_database.json"
        else:
            self.db_path = Path(db_path)
        self._data: Dict[str, Any] = {}
        self._load()

    def _load(self) -> None:
        if self.db_path.exists():
            with open(self.db_path, "r", encoding="utf-8") as f:
                self._data = json.load(f)
        else:
            self._data = {}

    def save(self) -> None:
        self.db_path.parent.mkdir(parents=True, exist_ok=True)
        with open(self.db_path, "w", encoding="utf-8") as f:
            json.dump(self._data, f, indent=2)

    @staticmethod
    def normalize_key(footprint: str, designation: str) -> str:
        fp = re.sub(r"\s+", "", footprint.strip().lower())
        des = re.sub(r"\s+", "", designation.strip().lower())
        return f"{fp}|{des}"

    def lookup(self, footprint: str, designation: str) -> Optional[Dict[str, Any]]:
        return self._data.get(self.normalize_key(footprint, designation))

    def add(self, footprint: str, designation: str, entry: Dict[str, Any]) -> None:
        key = self.normalize_key(footprint, designation)
        self._data[key] = entry

    def remove(self, footprint: str, designation: str) -> bool:
        return self._data.pop(self.normalize_key(footprint, designation), None) is not None

    def exclude(self, footprint: str, designation: str) -> None:
        self.add(footprint, designation, {
            "description": "DNO",
            "customer_part": "",
            "type": "other",
            "mouser_part": "DNO",
            "digikey_part": "DNO",
            "octopart_uid": "",
            "mcmaster_part": "",
            "sendcutsend_id": "",
            "manual_price": None,
            "price_currency": "USD",
            "price_updated": None,
            "notes": "Do not order",
        })

    def list_missing(self, items) -> list:
        """Return line items that have no database entry."""
        missing = []
        seen = set()
        for item in items:
            key = self.normalize_key(item.footprint, item.designation)
            if key in seen:
                continue
            seen.add(key)
            if key not in self._data:
                missing.append(item)
        return missing

    def all_entries(self) -> Dict[str, Any]:
        return self._data


class Inventory:
    """On-hand part counts (your leftover screws).

    Stored in bom_manager/data/inventory.json, which is gitignored: stock on
    your shelf is local state, not project data other builders should inherit.
    Keys use the same PartDatabase.normalize_key(footprint, designation).
    """

    def __init__(self, inv_path: Optional[Path] = None):
        if inv_path is None:
            inv_path = Path(__file__).resolve().parent / "data" / "inventory.json"
        self.inv_path = Path(inv_path)
        self._data: Dict[str, int] = {}
        self._load()

    def _load(self) -> None:
        if self.inv_path.exists():
            with open(self.inv_path, "r", encoding="utf-8") as f:
                raw = json.load(f)
            self._data = {k: int(v) for k, v in raw.items() if int(v) > 0}
        else:
            self._data = {}

    def save(self) -> None:
        self.inv_path.parent.mkdir(parents=True, exist_ok=True)
        with open(self.inv_path, "w", encoding="utf-8") as f:
            json.dump(self._data, f, indent=2, sort_keys=True)

    def get(self, footprint: str, designation: str) -> int:
        return self._data.get(PartDatabase.normalize_key(footprint, designation), 0)

    def set(self, footprint: str, designation: str, qty: int) -> None:
        key = PartDatabase.normalize_key(footprint, designation)
        if qty > 0:
            self._data[key] = int(qty)
        else:
            self._data.pop(key, None)

    def all_entries(self) -> Dict[str, int]:
        return dict(self._data)
