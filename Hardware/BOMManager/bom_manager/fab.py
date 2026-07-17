"""Fabrication package status: is everything ready to send out?

Collects, per chassis:
- each PCB board: Fab folder contents, built zip, fab price;
- each fabricated part (Mechanical/Fab): STEP, info.txt, price, image, rev, spec;
- each wiring harness: BOM CSV export present.
"""

import re
from dataclasses import dataclass, field
from pathlib import Path
from typing import List, Optional

from .context import Context


@dataclass
class BoardStatus:
    name: str
    fab_dir: Path
    fab_files: int
    bom_csv: bool
    zip_path: Optional[Path]
    price: Optional[float]

    @property
    def ready(self) -> bool:
        return self.bom_csv and self.fab_files > 0


@dataclass
class FabPartStatus:
    folder: Path
    name: str
    has_step: bool
    has_info: bool
    has_image: bool
    price: Optional[float]
    rev: str
    spec: str
    qty: int = 1

    @property
    def ready(self) -> bool:
        return self.has_step and self.has_info


@dataclass
class HarnessStatus:
    folder: Path
    name: str
    has_csv: bool
    qty: int = 1
    rev: str = ""


@dataclass
class FabStatus:
    chassis: str
    boards: List[BoardStatus] = field(default_factory=list)
    parts: List[FabPartStatus] = field(default_factory=list)
    harnesses: List[HarnessStatus] = field(default_factory=list)

    @property
    def problems(self) -> List[str]:
        out = []
        for b in self.boards:
            if not b.bom_csv:
                out.append(f"board {b.name}: no BOM CSV in Fab/")
            if b.price is None:
                out.append(f"board {b.name}: no fab price (fab pcb-price {b.name} <usd>)")
        for p in self.parts:
            if not p.has_info:
                out.append(f"fab part {p.name}: info.txt missing (import sendcutsend or fill by hand)")
            if not p.has_step:
                out.append(f"fab part {p.name}: no STEP file")
            if p.price is None:
                out.append(f"fab part {p.name}: no price")
        for h in self.harnesses:
            if not h.has_csv:
                out.append(f"harness {h.name}: no BOM CSV export (see its README)")
        return out


def _read_info(info_path: Path) -> dict:
    data = {}
    if info_path.exists():
        with open(info_path, "r", encoding="utf-8") as f:
            for line in f:
                line = line.strip()
                if line and not line.startswith("#") and "=" in line:
                    k, v = line.split("=", 1)
                    data[k.strip()] = v.strip()
    return data


def _board_price(ctx: Context, board: str) -> Optional[float]:
    entry = ctx.db.lookup("PCB", board)
    if entry and entry.get("manual_price") is not None:
        return entry["manual_price"]
    cached = ctx.cache.get("pcb", board)
    if cached and cached.unit_price is not None:
        return cached.unit_price
    return None


def friendly_name(folder: Path, kind: str = "") -> str:
    """Human-readable title for a board/harness/part folder.

    Prefers the schematic title-block title (the designer's own name for the
    thing); falls back to a prettified folder name. `kind` ("Wiring Harness",
    "PCB Assembly", ...) is prepended when the title doesn't already say it.
    """
    title = ""
    sch_files = sorted(folder.glob("*.kicad_sch"))
    for sch in sch_files:
        try:
            head = sch.read_text(encoding="utf-8", errors="replace")[:30000]
        except OSError:
            continue
        m = re.search(r'\(title\s+"([^"]+)"\)', head)
        if m and m.group(1).strip():
            title = m.group(1).strip()
            break
    if title:
        title = re.sub(r"^(open inverter platform|invertergen5)\s*[-–—]\s*", "", title, flags=re.IGNORECASE).strip()
        if title.isupper():
            title = title.title()
    if not title:
        pretty = re.sub(r"([a-z])([A-Z])", r"\1 \2", folder.name)
        pretty = re.sub(r"[-_]+", " ", pretty).strip()
        title = pretty.title() if pretty else folder.name
    if kind and kind.lower() not in title.lower():
        return f"{kind}: {title}"
    return title


def collect(ctx: Context, chassis: str, hardware_root: Optional[Path] = None) -> FabStatus:
    hw = hardware_root or ctx.hardware_root
    status = FabStatus(chassis=chassis)
    chassis_dir = hw / chassis

    boards_dir = chassis_dir / "Boards"
    if boards_dir.is_dir():
        for board_dir in sorted(boards_dir.iterdir()):
            if not board_dir.is_dir():
                continue
            fab_dir = board_dir / "Fab"
            if not fab_dir.is_dir():
                continue
            files = [f for f in fab_dir.iterdir() if f.is_file() and not f.name.startswith(".")]
            # BOM CSVs live in the project dir now; Fab/ holds gerbers.
            root_csv = board_dir / f"{board_dir.name}.csv"
            has_csv = root_csv.is_file() or any(f.suffix.lower() == ".csv" for f in files)
            zip_path = chassis_dir / "FabricationData" / "PCB_Fab_Zips" / f"{board_dir.name}.zip"
            status.boards.append(
                BoardStatus(
                    name=board_dir.name,
                    fab_dir=fab_dir,
                    fab_files=len(files),
                    bom_csv=has_csv,
                    zip_path=zip_path if zip_path.exists() else None,
                    price=_board_price(ctx, board_dir.name),
                )
            )

    mech_dir = chassis_dir / "Mechanical"
    for sub in ("Fab", "SendCutSendParts"):
        base = mech_dir / sub
        if not base.is_dir():
            continue
        for folder in sorted(base.iterdir()):
            if not folder.is_dir() or folder.name.startswith("."):
                continue
            info = _read_info(folder / "info.txt")
            steps = list(folder.glob("*.step")) + list(folder.glob("*.stp")) + list(folder.glob("*.STEP"))
            images = list(folder.glob("*.png")) + list(folder.glob("*.jpg")) + list(folder.glob("*.jpeg"))
            price = None
            if info.get("UnitPrice"):
                try:
                    price = float(info["UnitPrice"])
                except ValueError:
                    price = None
            try:
                qty = int(float(info.get("Qty", "1")))
            except ValueError:
                qty = 1
            spec = " / ".join(
                x for x in [info.get("Material", ""), info.get("Thickness_mm", "") and info["Thickness_mm"] + " mm",
                            info.get("Process", ""), info.get("Finish", "")] if x
            )
            status.parts.append(
                FabPartStatus(
                    folder=folder,
                    name=folder.name,
                    has_step=bool(steps),
                    has_info=bool(info),
                    has_image=bool(images),
                    price=price,
                    rev=info.get("Rev", ""),
                    spec=spec,
                    qty=qty,
                )
            )

    for harness_root in ("Wiring", "Harnesses"):
        harness_dir = chassis_dir / harness_root
        if not harness_dir.is_dir():
            continue
        for folder in sorted(harness_dir.iterdir()):
            if not folder.is_dir() or folder.name.startswith("."):
                continue
            has_csv = any(folder.glob("*.csv")) or any((folder / "Fab").glob("*.csv"))
            qty = 1
            csv = next(iter(sorted(folder.glob("*.csv")) or sorted((folder / "Fab").glob("*.csv"))), None)
            if csv is not None:
                from .parsers import harness_qty
                qty = harness_qty(csv)
            rev = ""
            from .descriptor_registry import DescriptorRegistry
            from .part_numbers import PartNumberRegistry
            abbr = DescriptorRegistry.abbreviate_chassis(chassis)
            entry = ctx.pn_registry.lookup(
                PartNumberRegistry._make_identity_key("wiring", f"wh|{folder.name.lower()}", abbr)
            )
            if entry:
                rev = entry.get("revision", "")
            status.harnesses.append(
                HarnessStatus(
                    folder=folder,
                    name=folder.name,
                    has_csv=has_csv,
                    qty=qty,
                    rev=rev,
                )
            )

    return status


def _check(cond: bool) -> str:
    return "x" if cond else " "


def sync_info_rev(ctx: Context, registry_key: str, rev: str) -> Optional[Path]:
    """Write Rev=<rev> into a fabricated part's info.txt after a rev bump.

    The part is found from its registry identity (fab:<folder name>, matched
    case-insensitively) under any chassis' Mechanical/Fab directory. The folder
    itself is never renamed. Returns the info.txt path on success.
    """
    if "|fab:" not in registry_key:
        return None
    identity = registry_key.split("|fab:", 1)[1]
    if not ctx.hardware_root.is_dir():
        return None
    for chassis_dir in ctx.hardware_root.iterdir():
        if not chassis_dir.is_dir() or not chassis_dir.name.lower().startswith("chassis"):
            continue
        for sub in ("Fab", "SendCutSendParts"):
            base = chassis_dir / "Mechanical" / sub
            if not base.is_dir():
                continue
            for folder in base.iterdir():
                if not folder.is_dir():
                    continue
                if re.sub(r"\s+", "", folder.name.lower()) != identity:
                    continue
                info_path = folder / "info.txt"
                lines = []
                if info_path.exists():
                    lines = info_path.read_text(encoding="utf-8").splitlines()
                for i, line in enumerate(lines):
                    if line.strip().startswith("Rev="):
                        lines[i] = f"Rev={rev}"
                        break
                else:
                    lines.append(f"Rev={rev}")
                info_path.write_text("\n".join(lines) + "\n", encoding="utf-8")
                return info_path
    return None


def render(status: FabStatus) -> str:
    """Plain-text fabrication package view for the shell."""
    out = [f"=== {status.chassis} fabrication package ===\n"]

    out.append(f"PCB boards ({len(status.boards)}):")
    for b in status.boards:
        zip_txt = "zip built" if b.zip_path else "zip not built (run: generate)"
        price_txt = f"${b.price:.2f}" if b.price is not None else "NO PRICE"
        out.append(f"  [{_check(b.ready)}] {b.name:<22} {b.fab_files} fab file(s), {zip_txt}, {price_txt}")
    if not status.boards:
        out.append("  (none)")

    out.append(f"\nFabricated parts ({len(status.parts)}):")
    for p in status.parts:
        marks = f"STEP:{_check(p.has_step)} info.txt:{_check(p.has_info)} img:{_check(p.has_image)}"
        price_txt = f"${p.price:.2f}" if p.price is not None else "NO PRICE"
        rev_txt = f" rev {p.rev}" if p.rev else ""
        out.append(f"  [{_check(p.ready)}] {p.name:<22} {marks}  qty {p.qty}  {price_txt}{rev_txt}")
        if p.spec:
            out.append(f"      {p.spec}")
    if not status.parts:
        out.append("  (none)")

    if status.harnesses:
        out.append(f"\nWiring harnesses ({len(status.harnesses)}):")
        for h in status.harnesses:
            rev_txt = f" rev {h.rev}" if h.rev else ""
            out.append(f"  [{_check(h.has_csv)}] {h.name:<22} qty {h.qty}{rev_txt}  {'BOM CSV present' if h.has_csv else 'no BOM CSV export'}")

    problems = status.problems
    out.append("")
    if problems:
        out.append(f"Not ready ({len(problems)}):")
        out.extend(f"  - {p}" for p in problems)
    else:
        out.append("All fabrication items are ready.")
    return "\n".join(out)


def render_markdown(status: FabStatus) -> List[str]:
    """Markdown section for the pricing report."""
    md = ["\n## Fabrication Package\n"]
    if status.boards:
        md.append("| PCB Board | Fab Files | Zip | Fab Price |")
        md.append("|-----------|-----------|-----|-----------|")
        for b in status.boards:
            zip_cell = "built" if b.zip_path else "missing"
            price_cell = f"${b.price:.2f}" if b.price is not None else "**missing**"
            md.append(f"| {b.name} | {b.fab_files} | {zip_cell} | {price_cell} |")
        md.append("")
    if status.parts:
        md.append("| Fab Part | Rev | Qty | STEP | info.txt | Price | Spec |")
        md.append("|----------|-----|-----|------|----------|-------|------|")
        for p in status.parts:
            step = "yes" if p.has_step else "**no**"
            info = "yes" if p.has_info else "**no**"
            price = f"${p.price:.2f}" if p.price is not None else "**missing**"
            md.append(f"| {p.name} | {p.rev or 'A'} | {p.qty} | {step} | {info} | {price} | {p.spec} |")
        md.append("")
    if status.harnesses:
        md.append("| Harness | Qty/Chassis | Rev | BOM CSV |")
        md.append("|---------|-------------|-----|---------|")
        for h in status.harnesses:
            md.append(f"| {h.name} | {h.qty} | {h.rev or 'A'} | {'yes' if h.has_csv else '**no**'} |")
        md.append("")
    problems = status.problems
    if problems:
        md.append("**Not ready:**")
        for p in problems:
            md.append(f"- {p}")
        md.append("")
    return md
