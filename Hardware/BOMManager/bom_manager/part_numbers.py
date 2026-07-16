"""Internal part number and revision registry."""

import json
import re
from pathlib import Path
from typing import Dict, Optional


CATEGORY_PREFIXES = {
    "pcb": "PCB",
    "wiring": "WH",
    "harness": "WH",
    "cable": "CBL",
    "busbar": "BB",
    "plate": "PLT",
    "bracket": "BRK",
    "3dprint": "3DP",
    "mechanical": "MECH",
    "fastener": "FST",
    "electrical": "ELEC",
    "connector": "CONN",
    "resistor": "RES",
    "capacitor": "CAP",
    "ic": "IC",
    "assembly": "ASSY",
    "other": "HW",
}


def _normalize_category(category: str) -> str:
    cat = category.lower().strip()
    synonyms = {
        "bus bar": "busbar",
        "busbar": "busbar",
        "wire": "wiring",
        "wires": "wiring",
        "harness": "wiring",
        "cable": "cable",
        "screw": "fastener",
        "bolt": "fastener",
        "nut": "fastener",
        "washer": "fastener",
        "standoff": "fastener",
        "3d print": "3dprint",
        "3d printed": "3dprint",
        "printed": "3dprint",
        "res": "resistor",
        "cap": "capacitor",
        "chip": "ic",
        "semiconductor": "ic",
    }
    return synonyms.get(cat, cat)


class PartNumberRegistry:
    """Generates and stores internal part numbers with revisions.

    Default descriptive format: HW-<CHASSIS>-<CATEGORY>-<DESCRIPTOR>-<REV>
    Example: HW-C2-PCB-CTRL-A

    Set via config.yaml:
      part_number:
        format: "HW-{chassis}-{category}-{descriptor}-{rev}"
    """

    DEFAULT_FORMAT = "HW-{chassis}-{category}-{descriptor}-{rev}"

    def __init__(self, registry_path: Optional[Path] = None, format: Optional[str] = None):
        if registry_path is None:
            self.registry_path = Path(__file__).resolve().parent / "data" / "part_numbers.json"
        else:
            self.registry_path = Path(registry_path)
        self.format = format or self.DEFAULT_FORMAT
        self._data: Dict[str, Dict] = {}
        self._load()

    def _load(self) -> None:
        if self.registry_path.exists():
            with open(self.registry_path, "r", encoding="utf-8") as f:
                self._data = json.load(f)
        else:
            self._data = {}

    def save(self) -> None:
        self.registry_path.parent.mkdir(parents=True, exist_ok=True)
        with open(self.registry_path, "w", encoding="utf-8") as f:
            json.dump(self._data, f, indent=2)

    def lookup(self, key: str) -> Optional[Dict]:
        return self._data.get(key)

    def lookup_by_pn(self, pn: str) -> Optional[Dict]:
        for entry in self._data.values():
            if entry.get("part_number") == pn:
                return entry
        return None

    @staticmethod
    def _make_key(category: str, description: str, chassis: Optional[str] = None) -> str:
        desc = re.sub(r"\s+", "_", description.strip().lower())[:80]
        prefix = f"{chassis.lower()}|" if chassis else ""
        return f"{prefix}{category.lower()}|{desc}"

    def generate_pn(
        self,
        category: str,
        description: str,
        descriptor: str,
        rev: str = "A",
        chassis: Optional[str] = None,
    ) -> str:
        """Generate or retrieve an existing part number for a key."""
        key = self._make_key(category, description, chassis)
        existing = self._data.get(key)
        if existing:
            return existing["part_number"]

        cat = _normalize_category(category)
        prefix = CATEGORY_PREFIXES.get(cat, "HW")
        descriptor = re.sub(r"[^A-Z0-9\-_]+", "-", descriptor.upper()).strip("-")[:24]
        rev = rev.upper()

        pn = self.format.format(
            category=prefix,
            descriptor=descriptor,
            rev=rev,
            chassis=chassis or "",
        )
        # Clean up double dashes from empty chassis if format omits it
        pn = re.sub(r"-+", "-", pn).strip("-")

        self._data[key] = {
            "part_number": pn,
            "category": cat,
            "category_prefix": prefix,
            "descriptor": descriptor,
            "revision": rev,
            "description": description,
            "key": key,
            "chassis": chassis,
        }
        return pn

    def bump_revision(self, key: str, new_rev: Optional[str] = None) -> Optional[str]:
        """Bump the revision of an existing part number."""
        entry = self._data.get(key)
        if not entry:
            return None
        if new_rev is None:
            old_rev = entry["revision"]
            if len(old_rev) == 1 and old_rev < "Z":
                new_rev = chr(ord(old_rev) + 1)
            else:
                new_rev = old_rev + "A"
        new_rev = new_rev.upper()
        entry["revision"] = new_rev
        base = entry["part_number"].rsplit("-", 1)[0]
        entry["part_number"] = f"{base}-{new_rev}"
        return entry["part_number"]

    def list_all(self) -> Dict[str, Dict]:
        return dict(self._data)
