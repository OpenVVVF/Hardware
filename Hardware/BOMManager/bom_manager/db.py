"""Part database: substitutions, vendor mappings, exclusions."""

import json
import re
from pathlib import Path
from typing import Any, Dict, Optional


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
