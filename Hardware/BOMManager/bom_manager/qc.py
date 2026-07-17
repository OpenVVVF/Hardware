"""Per-assembly breakdowns: parts per sub-assembly with quantities and costs,
plus printable QC sign-off forms (FabricationData/QC_Forms.pdf).

Assemblies are the boards, the wiring harnesses, the fabricated parts, and the
chassis hardware list. Per-assembly quantities come from the raw source items
(before cross-assembly merging), so shared parts show the qty each assembly
actually consumes.
"""

from dataclasses import dataclass, field
from functools import partial
from pathlib import Path
from typing import Dict, List, Optional

from reportlab.lib.units import inch, mm
from reportlab.platypus import PageBreak, SimpleDocTemplate, Spacer

from . import fab
from .addpart import collect_lines, existing_ipn, known_price
from .context import Context
from .discover import discover_boms
from .parsers import parse_source
from .pdfreport import (
    CONTENT_W, GREY, MARGIN, PAGE, _footer, _para, _rule, _table,
)

_KIND_LABEL = {
    "board": "PCB assembly",
    "harness": "wiring harness",
    "fab part": "fabricated part",
    "hardware": "chassis hardware",
}

_CHECK = "[  ]"


@dataclass
class AssemblyGroup:
    name: str
    kind: str
    title: str = ""
    ipn: str = ""
    rev: str = ""
    qty_per_chassis: int = 1
    items: Dict[str, int] = field(default_factory=dict)  # db key -> qty
    lines: Dict[str, object] = field(default_factory=dict)  # db key -> BomLine

    @property
    def kind_label(self) -> str:
        return _KIND_LABEL.get(self.kind, self.kind)


def assembly_groups(ctx: Context, chassis: str) -> List[AssemblyGroup]:
    """Parts split by sub-assembly with per-assembly quantities."""
    by_key = {}
    for ch, line in collect_lines(ctx, chassis):
        if not line.internal_pn:
            line.internal_pn = existing_ipn(ctx, ch, line)
        by_key[ctx.db.normalize_key(line.footprint, line.designation)] = line

    groups: Dict[str, AssemblyGroup] = {}
    order: List[str] = []
    for src in discover_boms(ctx.hardware_root, [chassis], None):
        kind = {
            "board": "board",
            "harness": "harness",
            "sendcutsend_folder": "fab part",
            "mechanical": "hardware",
        }.get(src.category)
        if not kind:
            continue
        if src.board not in groups:
            folder = src.path.parent if kind != "mechanical" else src.path
            groups[src.board] = AssemblyGroup(
                name=src.board, kind=kind,
                title=fab.friendly_name(folder, _KIND_LABEL[kind]) if kind != "mechanical"
                else "Chassis Hardware",
            )
            order.append(src.board)
        g = groups[src.board]
        for item in parse_source(src):
            key = ctx.db.normalize_key(item.footprint, item.designation)
            g.items[key] = g.items.get(key, 0) + item.quantity

    for name in order:
        g = groups[name]
        g.lines = {k: by_key.get(k) for k in g.items}
        for line in by_key.values():
            if g.kind == "board" and line.type == "pcb" and line.designation == name:
                g.ipn, g.rev = line.internal_pn, line.internal_pn.rsplit("-", 1)[-1]
            elif g.kind == "harness" and line.vendor_hint == "assembly" and line.designation == name:
                g.ipn, g.rev = line.internal_pn, line.internal_pn.rsplit("-", 1)[-1]
                g.qty_per_chassis = line.quantity
            elif (g.kind == "fab part" and line.vendor_hint == "sendcutsend"
                  and line.sources and line.sources[0].endswith(name)):
                g.ipn, g.rev = line.internal_pn, line.internal_pn.rsplit("-", 1)[-1]
                g.qty_per_chassis = line.quantity
    return [groups[n] for n in order]


def assembly_cost(ctx: Context, g: AssemblyGroup) -> float:
    """Per-assembly cost at per-piece prices (packed parts priced per piece)."""
    total = 0.0
    for key, qty in g.items.items():
        line = g.lines.get(key)
        if line is None:
            continue
        price, _ = known_price(ctx, line)
        if price is None:
            continue
        per_piece = price / line.pack_size if line.pack_size and line.pack_size > 1 else price
        total += per_piece * qty
    if g.kind == "board":
        board_price = fab._board_price(ctx, g.name)
        if board_price is not None:
            total += board_price
    return total


def assembly_costs(ctx: Context, groups: List[AssemblyGroup]) -> List[dict]:
    """Cost rows for the release report: assembly, kind, parts count, cost."""
    return [{
        "name": g.name, "title": g.title or g.name, "kind": g.kind_label,
        "parts": len(g.items), "qty": g.qty_per_chassis,
        "cost": assembly_cost(ctx, g),
    } for g in groups]


_INSPECTION = {
    "board": [
        "Visual inspection — no damage, contamination, or solder splash",
        "Solder joints — wetting, no bridges, no tombstoning",
        "IC / connector orientation (pin 1 marks match silkscreen)",
        "Continuity: no shorts across power rails (12 V, 5 V, 3.3 V)",
        "ESD precautions followed during assembly",
    ],
    "harness": [
        "Wire gauges and colors match the schematic",
        "Crimp quality — pull test one crimp per connector",
        "Pin-to-pin continuity against the schematic",
        "Connector keying / locking features engaged",
        "Length and routing match the harness drawing",
    ],
    "fab part": [
        "Dimensions match the drawing / STEP model",
        "Edges deburred; finish as specified in info.txt",
        "Flatness / bend angles within tolerance",
        "Threads and holes checked with mating hardware",
    ],
    "hardware": [
        "Correct part and quantity per line",
        "Fastener torque per specification",
        "Threadlocker applied where required",
    ],
}


def build_qc_forms(ctx: Context, chassis: str, out_pdf: Path) -> Optional[Path]:
    """One QC sign-off page per sub-assembly."""
    groups = assembly_groups(ctx, chassis)
    if not groups:
        return None

    story = []
    for idx, g in enumerate(groups):
        story.append(_para("QUALITY CONTROL — ASSEMBLY SIGN-OFF", size=9, color=GREY, bold=True))
        story.append(Spacer(1, 0.12 * inch))
        story.append(_para(g.title or g.name, size=18, bold=True))
        story.extend(_rule())
        meta = [["Assembly", g.kind_label, "Qty per chassis", str(g.qty_per_chassis)],
                ["Internal P/N", g.ipn or "—", "Revision", g.rev or "A"]]
        story.append(_table(meta, [30 * mm, 65 * mm, 34 * mm, 30 * mm], header=False, size=9))
        story.append(Spacer(1, 0.15 * inch))

        rows = [[_CHECK, "Qty", "Description", "Part Number"]]
        for key, qty in sorted(g.items.items(), key=lambda kv: kv[0]):
            line = g.lines.get(key)
            desc = line.description if line else key.split("|", 1)[1]
            pn = (line.vendor_part_number(line.primary_vendor()) or line.designation) if line else ""
            rows.append([_CHECK, str(qty), desc, pn])
        story.append(_para("Parts", size=11, bold=True))
        story.append(Spacer(1, 3))
        story.append(_table(rows, [10 * mm, 12 * mm, 90 * mm, 50 * mm], size=8))
        story.append(Spacer(1, 0.15 * inch))

        story.append(_para("Inspection", size=11, bold=True))
        story.append(Spacer(1, 3))
        insp = [[_CHECK, "Check"]] + [[_CHECK, c] for c in _INSPECTION[g.kind]]
        story.append(_table(insp, [10 * mm, 150 * mm], size=8))
        story.append(Spacer(1, 0.35 * inch))

        sign = [
            ["Assembled by:", "", "Date:", ""],
            ["Inspected by:", "", "Date:", ""],
            ["Signature:", "\n\n", "Signature:", "\n\n"],
        ]
        story.append(_table(sign, [32 * mm, 56 * mm, 32 * mm, 56 * mm], header=False, size=9))
        if idx < len(groups) - 1:
            story.append(PageBreak())

    out_pdf.parent.mkdir(parents=True, exist_ok=True)
    doc = SimpleDocTemplate(str(out_pdf), pagesize=PAGE, leftMargin=MARGIN, rightMargin=MARGIN)
    doc.build(story, onFirstPage=partial(_footer, chassis=chassis),
              onLaterPages=partial(_footer, chassis=chassis))
    return out_pdf
