"""Internal part number and revision registry.

Registry keys are identity-based so that user-editable text (descriptions,
friendly PartNames) never silently renumbers a part:

- most parts:        <chassis>|<category>|<footprint>|<designation>
- fabricated parts:  <chassis>|<category>|fab:<folder name>

Older registries used <chassis>|<category>|<description>; entries are migrated
on first contact (looked up under the legacy key, moved to the identity key,
keeping the assigned part number, revision, and history).
"""

import json
import re
from datetime import datetime
from pathlib import Path
from typing import Dict, List, Optional, Tuple


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


def line_identity(line) -> str:
    """Stable identity string for a BOM line.

    Fabricated (SendCutSend folder) parts are identified by their folder name,
    which never changes; everything else by footprint|designation, matching
    the part-database key.
    """
    if line.vendor_hint == "sendcutsend" and line.sources:
        folder = line.sources[0].split("/")[-1]
        return f"fab:{re.sub(r' ', '', folder.strip().lower())}"
    fp = re.sub(r"\s+", "", line.footprint.strip().lower())
    des = re.sub(r"\s+", "", line.designation.strip().lower())
    return f"{fp}|{des}"


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
        """Legacy (pre-identity) key form, kept for migration lookups."""
        desc = re.sub(r"\s+", "_", description.strip().lower())[:80]
        prefix = f"{chassis.lower()}|" if chassis else ""
        return f"{prefix}{category.lower()}|{desc}"

    @staticmethod
    def _make_identity_key(category: str, identity: str, chassis: Optional[str] = None) -> str:
        prefix = f"{chassis.lower()}|" if chassis else ""
        return f"{prefix}{category.lower()}|{identity}"

    def _rekey(self, old_key: str, new_key: str, entry: Dict) -> None:
        if old_key != new_key:
            self._data.pop(old_key, None)
            entry["key"] = new_key
            self._data[new_key] = entry

    def resolve_entry(
        self,
        category: str,
        description: str,
        descriptor: str,
        chassis: Optional[str],
        identity: Optional[str] = None,
    ) -> Tuple[str, Optional[Dict]]:
        """Find the entry for a line without creating one.

        Lookup chain: identity key -> legacy description key -> chassis +
        category + descriptor scan (covers description edits). Matches under a
        stale key are migrated to the identity key.
        """
        key = self._make_identity_key(category, identity or description, chassis)
        entry = self._data.get(key)
        if entry:
            return key, entry

        legacy_key = self._make_key(category, description, chassis)
        if legacy_key != key:
            entry = self._data.get(legacy_key)
            if entry:
                self._rekey(legacy_key, key, entry)
                return key, entry

        cat = _normalize_category(category)
        for k, e in list(self._data.items()):
            if (
                e.get("chassis") == chassis
                and e.get("category") == cat
                and e.get("descriptor") == descriptor
            ):
                self._rekey(k, key, e)
                return key, e
        return key, None

    def generate_pn(
        self,
        category: str,
        description: str,
        descriptor: str,
        rev: str = "A",
        chassis: Optional[str] = None,
        identity: Optional[str] = None,
    ) -> str:
        """Generate or retrieve an existing part number for a line."""
        key, existing = self.resolve_entry(category, description, descriptor, chassis, identity)
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
            "history": [
                {
                    "rev": rev,
                    "date": datetime.utcnow().date().isoformat(),
                    "note": "created",
                }
            ],
        }
        return pn

    def find(self, query: str) -> List[Dict]:
        """Fuzzy-find registry entries by part number, descriptor, or description."""
        q = query.strip().lower()
        if not q:
            return []
        exact = [e for e in self._data.values() if e.get("part_number", "").lower() == q]
        if exact:
            return exact
        return [
            e
            for e in self._data.values()
            if q in e.get("part_number", "").lower()
            or q in e.get("descriptor", "").lower()
            or q in e.get("description", "").lower()
        ]

    def bump_revision(
        self, key: str, new_rev: Optional[str] = None, note: str = ""
    ) -> Optional[str]:
        """Bump the revision of an existing part number, recording history."""
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
        entry.setdefault("history", []).append(
            {
                "rev": new_rev,
                "date": datetime.utcnow().date().isoformat(),
                "note": note,
            }
        )
        return entry["part_number"]

    def list_all(self) -> Dict[str, Dict]:
        return dict(self._data)
