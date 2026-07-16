"""Persistent descriptor registry for human-readable part numbers.

Maps (chassis, category, description) -> descriptor. When a descriptor is not
yet known, the registry can prompt the user with a default and remember the
answer for subsequent runs.
"""

import json
import re
import sys
from pathlib import Path
from typing import Dict, Optional, Tuple


class DescriptorRegistry:
    """Stores and prompts for part-number descriptors.

    Registry file: bom_manager/data/part_descriptors.json
    Format: {"chassis|category|description": "DESCRIPTOR", ...}
    """

    def __init__(self, registry_path: Optional[Path] = None, allow_prompt: bool = True):
        if registry_path is None:
            self.registry_path = Path(__file__).resolve().parent / "data" / "part_descriptors.json"
        else:
            self.registry_path = Path(registry_path)
        self.allow_prompt = allow_prompt
        self._data: Dict[str, str] = {}
        self._load()

    def _key(self, chassis: Optional[str], category: str, description: str) -> str:
        desc = re.sub(r"\s+", "_", description.strip().lower())
        return f"{chassis or ''}|{category.lower()}|{desc}"

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

    def get(self, chassis: Optional[str], category: str, description: str) -> Optional[str]:
        return self._data.get(self._key(chassis, category, description))

    def set(self, chassis: Optional[str], category: str, description: str, descriptor: str) -> None:
        self._data[self._key(chassis, category, description)] = descriptor.strip().upper()

    def all_entries(self) -> Dict[str, str]:
        return dict(self._data)

    @staticmethod
    def _clean_descriptor(text: str) -> str:
        """Normalize a user-supplied descriptor: uppercase, alphanumerics and hyphens only."""
        text = text.strip().upper()
        text = re.sub(r"[^A-Z0-9\-_]+", "-", text)
        text = re.sub(r"-+", "-", text).strip("-")
        return text[:24]

    @staticmethod
    def abbreviate_chassis(chassis: str) -> str:
        """Convert 'Chassis2' -> 'C2', 'Chassis3' -> 'C3'."""
        m = re.match(r"chassis\s*(\d+)", chassis, re.IGNORECASE)
        if m:
            return f"C{m.group(1)}"
        return chassis.upper()[:6]

    @staticmethod
    def default_for_board(board_name: str) -> str:
        """Derive a short default descriptor from a board folder name."""
        name = re.sub(r"([a-z])([A-Z])", r"\1_\2", board_name)
        parts = re.split(r"[\s_\-]+", name.upper())
        joined = "".join(parts)

        # Exact board-name mappings
        board_abbrevs = {
            "CONTROLBOARD": "CTRL",
            "GATEDRIVER": "GD",
            "GATEDRIVERBOARD": "GD",
            "DCBUSCAPACITORBOARD": "DCBUSCAP",
            "DCBUSFILTER": "DCFLT",
            "DCBUSFILTERBOARD": "DCFLT",
            "IOBOARD": "IO",
            "INPUTOUTPUTBOARD": "IO",
        }
        if joined in board_abbrevs:
            return board_abbrevs[joined]

        # Component word abbreviations (used as fallback)
        word_abbrevs = {
            "CONTROL": "CTRL",
            "CONTROLLER": "CTRL",
            "GATE": "G",
            "DRIVER": "D",
            "DCBUS": "DCBUS",
            "CAPACITOR": "CAP",
            "FILTER": "FLT",
            "INPUT": "IN",
            "OUTPUT": "OUT",
            "IO": "IO",
        }
        for part in parts:
            if part in word_abbrevs:
                return word_abbrevs[part]

        # Build acronym from capital letters
        acronym = "".join(p[0] for p in parts if p)
        return acronym[:12] or board_name.upper()[:12]

    @staticmethod
    def default_for_harness(name: str) -> str:
        """Derive a default descriptor from a wiring-harness folder/document name."""
        return re.sub(r"[^A-Z0-9\-_]+", "-", name.upper()).strip("-")[:24]

    @staticmethod
    def default_for_commodity(
        category: str, description: str, footprint: str = "", designation: str = ""
    ) -> str:
        """Auto-derive a descriptor for resistors, capacitors, ICs, etc."""
        cat = category.lower()
        des = description.strip().upper()
        desig = designation.strip().upper()
        fp = footprint.upper()

        # Extract footprint size (1210, 0805, etc.)
        size_match = re.search(r"\b(\d{4})\b", fp)
        size = size_match.group(1) if size_match else ""

        # Normalize decimals: 2.2 -> 2R2 / 2U2 to avoid dots/hyphens
        def _norm_decimal(text: str, is_resistor: bool = False) -> str:
            text = text.replace("Ω", "OHM")
            if is_resistor:
                # 2.7 ohm -> 2.7R -> 2R7
                text = re.sub(r"(\d)\.?(\d*)\s*OHM", r"\1R\2", text, flags=re.IGNORECASE)
                # Plain ohms with a decimal: 2.7 -> 2R7
                text = re.sub(r"(\d)\.(\d)(?![A-Z])", r"\1R\2", text)
            text = re.sub(r"(\d)\.(\d)\s*([KMNPU])", r"\1\3\2", text, flags=re.IGNORECASE)
            return text

        def _tokenize(text: str) -> list:
            return [t for t in re.split(r"[\s_\-]+", text) if t]

        def _looks_like_value(text: str) -> bool:
            return bool(re.search(r"\d", text))

        if cat == "resistor":
            # Prefer designation (value) if it looks like one, else description
            text = desig if _looks_like_value(desig) else des
            tokens = _tokenize(text)
            parts = []
            for t in tokens:
                t = t.replace("OHM", "").replace("Ω", "")
                t = _norm_decimal(t, is_resistor=True)
                t = re.sub(r"[^A-Z0-9KMR]", "", t)
                if t and t not in parts:
                    parts.append(t)
            if size and size not in parts:
                parts.append(size)
            return "-".join(parts)[:24]

        if cat == "capacitor":
            text = desig if _looks_like_value(desig) else des
            tokens = _tokenize(text)
            parts = []
            for t in tokens:
                t = t.replace("F", "")
                t = _norm_decimal(t)
                t = re.sub(r"[^A-Z0-9KNPUMV]", "", t)
                if t and t not in parts:
                    parts.append(t)
            if size and size not in parts:
                parts.append(size)
            return "-".join(parts)[:24]

        if cat in ("ic", "chip"):
            # Prefer designation if it looks like an MPN, else description
            for candidate in (desig, des):
                mpn = re.sub(r"[^A-Z0-9\-_]", "", candidate)
                if len(mpn) >= 4 and re.search(r"\d", mpn):
                    return mpn[:24]
            slug = re.sub(r"[^A-Z0-9\-_]+", "-", des).strip("-")[:24]
            return slug

        if cat == "connector":
            return re.sub(r"[^A-Z0-9\-_]", "-", des).strip("-")[:24]

        # Fallback: slugify the description
        return re.sub(r"[^A-Z0-9\-_]+", "-", des).strip("-")[:24]

    @staticmethod
    def default_for_mechanical(description: str) -> str:
        """Use McMaster PN or slugified description as descriptor."""
        mpn_match = re.search(r"\b([A-Z0-9]{5,12})\b", description.upper())
        if mpn_match and re.search(r"[A-Z]", mpn_match.group(1)) and re.search(r"\d", mpn_match.group(1)):
            return mpn_match.group(1)[:24]
        return re.sub(r"[^A-Z0-9\-_]+", "-", description.upper()).strip("-")[:24]

    def get_or_prompt(
        self,
        chassis: Optional[str],
        category: str,
        description: str,
        default: str = "",
        reason: str = "",
    ) -> str:
        """Return a descriptor, prompting the user if one is not yet stored."""
        saved = self.get(chassis, category, description)
        if saved:
            return saved

        default = self._clean_descriptor(default)
        if not default:
            default = "PART"

        if not self.allow_prompt:
            raise RuntimeError(
                f"Missing descriptor for {chassis or 'global'} {category} '{description}'. "
                f"Run interactively once, or edit {self.registry_path} and add a mapping."
            )

        print(f"\nNeed a descriptor for {chassis or 'global'} {category}: {description}")
        if reason:
            print(f"  ({reason})")
        print(f"  Default: {default}")
        try:
            answer = input("  Descriptor (Enter to accept default): ").strip()
        except EOFError:
            answer = ""
        descriptor = self._clean_descriptor(answer) if answer else default
        if not descriptor:
            descriptor = default

        self.set(chassis, category, description, descriptor)
        self.save()
        print(f"  Saved descriptor: {descriptor}")
        return descriptor
