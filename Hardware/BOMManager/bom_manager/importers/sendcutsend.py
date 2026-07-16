"""Import SendCutSend cart/order text and generate/update info.txt files.

Reads pasted SendCutSend cart text (from a file or stdin), extracts part
details, and writes info.txt files into the matching folders under
Hardware/Chassis*/Mechanical/Fab/.
"""

import argparse
import re
import sys
from pathlib import Path

from ..context import Context


def normalize_name(name: str) -> str:
    """Normalize a part name/folder name for matching."""
    return re.sub(r"[\s_]+", "-", name.strip().upper()).strip("-")


def parse_cart(text: str) -> list:
    """Parse SendCutSend cart text into a list of part dicts."""
    parts = []
    # Split into candidate blocks by .step occurrences
    blocks = re.split(r"(\S+\.step)\s*", text)

    for i in range(1, len(blocks), 2):
        filename = blocks[i].strip()
        nickname = Path(filename).stem
        body = blocks[i + 1] if i + 1 < len(blocks) else ""

        # Try to find a nickname line before the .step line, but only if it looks
        # like a part nickname (alphanumeric with dashes/underscores) rather than
        # a person's name or UI label.
        preceding = text[:text.find(filename)]
        nickname_candidates = [line.strip() for line in preceding.splitlines() if line.strip()]
        if nickname_candidates:
            candidate = nickname_candidates[-1]
            if re.match(r"^[A-Za-z0-9][A-Za-z0-9_\-]*$", candidate):
                nickname = candidate

        part = {
            "nickname": nickname,
            "filename": filename,
            "part_name": nickname,
            "process": None,
            "dimensions_in": None,
            "material": None,
            "thickness_in": None,
            "services": [],
            "unit_price": None,
            "total_price": None,
            "qty": 1,
        }

        # Lines after the filename
        lines = [line.strip() for line in body.splitlines() if line.strip()]

        for idx, line in enumerate(lines):
            lower = line.lower()

            if "sheet cutting" in lower or "cnc" in lower or "waterjet" in lower:
                part["process"] = line
                continue

            # Dimensions: "12.357 x 0.551 in"
            dim_match = re.search(r"(\d+\.?\d*)\s*x\s*(\d+\.?\d*)\s*in", line, re.IGNORECASE)
            if dim_match:
                part["dimensions_in"] = f"{dim_match.group(1)}x{dim_match.group(2)} in"
                continue

            # Material: "Copper (.187")" or "Aluminum 5052 (.25")"
            mat_match = re.search(r"(.+?)\s*\(\s*(\d*\.?\d+)\s*\"\s*\)", line)
            if mat_match:
                part["material"] = mat_match.group(1).strip()
                part["thickness_in"] = mat_match.group(2).strip()
                continue

            # Services (common ones)
            if any(s in lower for s in ["bending", "tapping", "deburring", "anodizing", "tinning"]):
                if line not in part["services"]:
                    part["services"].append(line)
                continue

            # Pricing
            each_match = re.search(r"[Ee]ach:\s*\$?([0-9,]+\.\d{2})", line)
            if each_match:
                part["unit_price"] = float(each_match.group(1).replace(",", ""))
                continue

            total_match = re.search(r"[Tt]otal:\s*\$?([0-9,]+\.\d{2})", line)
            if total_match:
                part["total_price"] = float(total_match.group(1).replace(",", ""))
                continue

        if part["unit_price"] and part["total_price"]:
            part["qty"] = max(1, int(round(part["total_price"] / part["unit_price"])))

        parts.append(part)

    return parts


def _name_tokens(name: str) -> frozenset:
    """Return the set of alphanumeric tokens in a name, case-insensitive."""
    return frozenset(re.sub(r"[^a-z0-9]+", " ", name.lower()).split())


def find_part_folder(chassis_dir: Path, nickname: str) -> Path:
    """Find a matching fabricated-part folder under Mechanical/Fab or SendCutSendParts.

    Matches are order-independent so C2_HW_BSP_A matches HW-C2-BSP-A.
    """
    target_tokens = _name_tokens(nickname)
    best_match: Path = None
    best_score = 0
    for sub in ("Fab", "SendCutSendParts"):
        base = chassis_dir / "Mechanical" / sub
        if not base.is_dir():
            continue
        for child in base.iterdir():
            if not child.is_dir() or child.name.startswith("."):
                continue
            child_tokens = _name_tokens(child.name)
            # Exact token set match is best
            if child_tokens == target_tokens:
                return child
            # Otherwise prefer folders that share the most tokens
            common = len(child_tokens & target_tokens)
            if common > best_score:
                best_score = common
                best_match = child
    return best_match


def inches_to_mm(inches: float) -> float:
    return inches * 25.4


def write_info_txt(folder: Path, part: dict, dry_run: bool = False) -> None:
    """Write or update info.txt in the given folder."""
    info_path = folder / "info.txt"

    # Preserve existing friendly PartName and notes if present
    existing_notes = ""
    existing_part_name = ""
    existing_rev = ""
    if info_path.exists():
        with open(info_path, "r", encoding="utf-8") as f:
            for line in f:
                stripped = line.strip()
                if stripped.startswith("Notes="):
                    existing_notes = stripped.split("=", 1)[1]
                elif stripped.startswith("PartName="):
                    existing_part_name = stripped.split("=", 1)[1]
                elif stripped.startswith("Rev="):
                    existing_rev = stripped.split("=", 1)[1]

    thickness_mm = ""
    if part.get("thickness_in"):
        try:
            thickness_mm = f"{inches_to_mm(float(part['thickness_in'])):.2f}"
        except ValueError:
            thickness_mm = part["thickness_in"]

    # The folder name is the part name. Normalize it to hyphenated uppercase.
    part_name = normalize_name(folder.name)
    if existing_part_name and existing_part_name.strip():
        # Preserve a manually-set friendly name only if it differs meaningfully,
        # otherwise keep the folder name as the canonical PN.
        if normalize_name(existing_part_name) != part_name:
            part_name = existing_part_name.strip()
    lines = [
        f"PartName={part_name}",
        f"Material={part.get('material', '')}",
        f"Thickness_mm={thickness_mm}",
        f"Qty={part.get('qty', 1)}",
        f"Dimensions_in={part.get('dimensions_in', '')}",
        f"Finish={', '.join(part.get('services', []))}",
        f"UnitPrice={part.get('unit_price', '')}",
        f"Process={part.get('process', '')}",
    ]
    if existing_rev:
        lines.append(f"Rev={existing_rev}")
    notes = existing_notes or f"Imported from SendCutSend cart"
    lines.append(f"Notes={notes}")

    content = "\n".join(lines) + "\n"

    if dry_run:
        print(f"\nWould write {info_path}:\n{content}")
        return

    info_path.parent.mkdir(parents=True, exist_ok=True)
    with open(info_path, "w", encoding="utf-8") as f:
        f.write(content)
    print(f"Wrote {info_path}")


def run(argv, ctx: Context, chassis: str = None) -> int:
    parser = argparse.ArgumentParser(prog="import sendcutsend", description="Import SendCutSend cart text into info.txt files.")
    parser.add_argument("input", nargs="?", help="Cart text file (default: stdin)")
    parser.add_argument("--chassis", default=None, help="Chassis folder name (default: current)")
    parser.add_argument("--dry-run", action="store_true", help="Print what would be written without writing")
    args = parser.parse_args(argv)

    chassis = args.chassis or chassis
    if not chassis:
        print("No chassis selected. Use --chassis or the 'chassis' command.", file=sys.stderr)
        return 1

    if args.input:
        text = Path(args.input).read_text(encoding="utf-8")
    else:
        print("Paste SendCutSend cart text, then press Ctrl+D:", file=sys.stderr)
        text = sys.stdin.read()

    if not text.strip():
        print("No input text provided.", file=sys.stderr)
        return 1

    parts = parse_cart(text)
    if not parts:
        print("No parts found in input.", file=sys.stderr)
        return 1

    chassis_dir = ctx.hardware_root / chassis

    if not chassis_dir.is_dir():
        print(f"Chassis directory not found: {chassis_dir}", file=sys.stderr)
        return 1

    print(f"Found {len(parts)} part(s) in cart text.")
    for part in parts:
        folder = find_part_folder(chassis_dir, part["nickname"])
        if not folder:
            # Suggest creating folder from nickname
            suggested = chassis_dir / "Mechanical" / "Fab" / part["nickname"]
            if args.dry_run:
                print(f"\nWould create folder: {suggested}")
                folder = suggested
            else:
                suggested.mkdir(parents=True, exist_ok=True)
                folder = suggested
                print(f"Created folder: {suggested}")
        write_info_txt(folder, part, dry_run=args.dry_run)

    return 0
