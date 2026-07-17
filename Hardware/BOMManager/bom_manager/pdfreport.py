"""Full release PDF report: cover, pricing, fabrication package, and per-board /
per-harness sections with divider pages, schematics, and PCB layer plots.

Generated pages are built with reportlab; schematic and layer PDFs are merged
in order with pdfunite. Board layer plots come from KiCad CLI (flatpak).
"""

import re
import subprocess
import sys
import tempfile
from datetime import datetime
from functools import partial
from pathlib import Path
from typing import Dict, List, Optional

from reportlab.lib import colors
from reportlab.lib.pagesizes import letter
from reportlab.lib.styles import getSampleStyleSheet
from reportlab.lib.units import inch
from reportlab.platypus import (
    HRFlowable, Image, PageBreak, Paragraph, SimpleDocTemplate, Spacer, Table,
    TableStyle,
)

from . import fab
from .addpart import collect_lines, existing_ipn, known_price
from .context import Context
from .pricing import PricingEngine, line_total

PAGE = letter
MARGIN = 0.75 * inch
CONTENT_W = PAGE[0] - 2 * MARGIN
STYLES = getSampleStyleSheet()

ACCENT = colors.HexColor("#16385C")
ACCENT_LIGHT = colors.HexColor("#EAF0F6")
GREY = colors.HexColor("#5A6570")
GRID = colors.HexColor("#B9C2CC")

VENDOR_DISPLAY = {
    "mcmaster": "McMaster-Carr",
    "mouser": "Mouser",
    "digikey": "Digi-Key",
    "sendcutsend": "SendCutSend",
    "pcb": "PCB Fabrication",
    "assembly": "In-House Assembly",
    "unknown": "Unknown / Missing",
}


def _para(text, style="Normal", size=9, color=None, markup=False, align=None):
    st = STYLES[style].clone(f"p{size}{style}{color}")
    st.fontSize = size
    st.leading = size + 2.5
    if color is not None:
        st.textColor = color
    if align is not None:
        st.alignment = align
    if not markup:
        text = str(text).replace("&", "&amp;").replace("<", "&lt;").replace(">", "&gt;")
    return Paragraph(str(text), st)


def _band(text, sub=None):
    """Full-width accent band used on cover and section dividers."""
    rows = [[_para(text, size=20, color=colors.white)]]
    if sub:
        rows.append([_para(sub, size=10, color=colors.HexColor("#C9D6E4"))])
    t = Table(rows, colWidths=[CONTENT_W])
    t.setStyle(TableStyle([
        ("BACKGROUND", (0, 0), (-1, -1), ACCENT),
        ("LEFTPADDING", (0, 0), (-1, -1), 16),
        ("TOPPADDING", (0, 0), (-1, 0), 14),
        ("BOTTOMPADDING", (0, -1), (-1, -1), 14),
    ]))
    return t


def _rule(space_before=4, space_after=8, thickness=1.2, color=ACCENT):
    return [
        Spacer(1, space_before),
        HRFlowable(width="100%", thickness=thickness, color=color),
        Spacer(1, space_after),
    ]


def _section_header(story, text):
    story.append(_para(text, size=15, color=ACCENT, style="Heading1"))
    story.extend(_rule())


def _table(rows, widths, header=True, size=8):
    header_row = [[_para(c, size=size, color=colors.white) for c in rows[0]]] if header else []
    body = [[_para(c, size=size) for c in row] for row in (rows[1:] if header else rows)]
    t = Table(header_row + body, colWidths=widths, repeatRows=1 if header else 0)
    style = [
        ("GRID", (0, 0), (-1, -1), 0.4, GRID),
        ("VALIGN", (0, 0), (-1, -1), "TOP"),
        ("TOPPADDING", (0, 0), (-1, -1), 3),
        ("BOTTOMPADDING", (0, 0), (-1, -1), 3),
        ("LEFTPADDING", (0, 0), (-1, -1), 5),
    ]
    if header:
        style += [
            ("BACKGROUND", (0, 0), (-1, 0), ACCENT),
            ("ROWBACKGROUNDS", (0, 1), (-1, -1), [colors.white, ACCENT_LIGHT]),
        ]
    t.setStyle(TableStyle(style))
    return t


def _images_row(paths, max_h=2.6 * inch, max_w=3.3 * inch):
    imgs = []
    for p in paths:
        try:
            img = Image(str(p))
            ratio = min(max_w / img.imageWidth, max_h / img.imageHeight, 1.0)
            img.drawWidth = img.imageWidth * ratio
            img.drawHeight = img.imageHeight * ratio
            imgs.append(img)
        except Exception:
            continue
    if not imgs:
        return None
    t = Table([imgs])
    t.setStyle(TableStyle([("ALIGN", (0, 0), (-1, -1), "CENTER")]))
    return t


def _footer(canvas, doc, chassis):
    canvas.saveState()
    canvas.setFont("Helvetica", 8)
    canvas.setFillColor(GREY)
    canvas.drawString(MARGIN, 0.45 * inch, f"Inverter Gen5 — {chassis} Hardware Release")
    canvas.drawRightString(PAGE[0] - MARGIN, 0.45 * inch, f"Page {doc.page}")
    canvas.restoreState()


# ---------------------------------------------------------------- front matter

def _cover(story, ctx, chassis, priced, grand_total, scaling):
    priced_count = sum(1 for e in priced if e["price"].unit_price is not None)
    story.append(Spacer(1, 1.2 * inch))
    story.append(_band("INVERTER GEN5", "OPEN INVERTER PLATFORM"))
    story.append(Spacer(1, 0.5 * inch))
    story.append(_para("Hardware Release Report", size=24, color=ACCENT, style="Title"))
    story.extend(_rule(space_before=6, space_after=16))
    info = [
        ["Chassis", chassis],
        ["Generated", datetime.now().date().isoformat()],
        ["BOM lines", f"{len(priced)} ({priced_count} priced)"],
    ]
    story.append(_table([["", ""]] + info, [1.4 * inch, 3.2 * inch], header=False, size=10))
    story.append(Spacer(1, 0.45 * inch))
    story.append(_para("Cost at quantity", size=12, color=ACCENT, style="Heading2"))
    story.append(Spacer(1, 4))
    rows = [["Units", "Estimated total"]] + [[str(q), f"${t:,.2f}"] for q, t in scaling]
    story.append(_table(rows, [1.1 * inch, 1.8 * inch], size=10))
    story.append(PageBreak())


def _pricing_sections(story, priced, qty=1):
    groups: Dict[str, list] = {}
    for entry in priced:
        vendor = entry["line"].primary_vendor() or "unknown"
        groups.setdefault(vendor, []).append(entry)

    _section_header(story, "Order Summary")
    for vendor in sorted(groups):
        display = VENDOR_DISPLAY.get(vendor, vendor.title())
        rows = [["Qty", "Internal P/N", "Description", "Part Number", "Unit", "Total"]]
        subtotal = 0.0
        for e in sorted(groups[vendor], key=lambda x: x["line"].description.lower()):
            line, price = e["line"], e["price"]
            need = line.quantity * qty
            ext = line_total(line, price.unit_price, qty)
            subtotal += ext
            packed = line.pack_size and line.pack_size > 1
            rows.append([
                f"{need}" + (f" ({line.packs_needed(need)}pk)" if packed else ""),
                line.internal_pn or "-",
                line.description,
                price.part_number or "-",
                f"${price.unit_price:,.4f}" + ("/pk" if packed else "") if price.unit_price is not None else "N/A",
                f"${ext:,.2f}" if price.unit_price is not None else "N/A",
            ])
        rows.append(["", "", "", "", "Subtotal", f"${subtotal:,.2f}"])
        story.append(Spacer(1, 0.12 * inch))
        story.append(_para(display, size=11, color=ACCENT, style="Heading2"))
        story.append(Spacer(1, 3))
        story.append(_table(rows, [0.55 * inch, 1.5 * inch, 2.05 * inch, 1.3 * inch, 0.9 * inch, 0.7 * inch]))
    story.append(PageBreak())


def _fab_section(story, ctx, chassis):
    status = fab.collect(ctx, chassis)
    _section_header(story, "Fabrication Package")
    rows = [["Item", "Kind", "Qty", "Rev", "Price", "Ready"]]
    for b in status.boards:
        rows.append([b.name, "PCB", "1", "A", f"${b.price:,.2f}" if b.price is not None else "MISSING",
                     "yes" if b.ready else "NO"])
    for p in status.parts:
        rows.append([p.name, "fab part", str(p.qty), p.rev or "A",
                     f"${p.price:,.2f}" if p.price is not None else "MISSING",
                     "yes" if p.ready else "NO"])
    for h in status.harnesses:
        rows.append([h.name, "harness", "-", "-", "-", "yes" if h.has_csv else "NO CSV"])
    story.append(_table(rows, [1.9 * inch, 0.8 * inch, 0.5 * inch, 0.5 * inch, 0.9 * inch, 0.7 * inch]))
    problems = status.problems
    if problems:
        story.append(Spacer(1, 0.1 * inch))
        story.append(_para("<b>Not ready:</b> " + "; ".join(problems), size=9, style="Italic", markup=True))
    story.append(PageBreak())

    for p in status.parts:
        if not p.has_info:
            continue
        story.append(_para(p.name, size=13, color=ACCENT, style="Heading2"))
        story.extend(_rule(thickness=0.8, space_after=6))
        story.append(_para(p.spec or "-", size=9, color=GREY))
        story.append(Spacer(1, 0.08 * inch))
        imgs = sorted(p.folder.glob("*.png")) + sorted(p.folder.glob("*.jpg"))
        row = _images_row(imgs[:3])
        if row:
            story.append(row)
        story.append(PageBreak())


# ------------------------------------------------------------------- sections

def _divider_pdf(path: Path, title: str, ipn: str, kind: str, info_rows, image_paths: List[Path], chassis: str) -> None:
    doc = SimpleDocTemplate(str(path), pagesize=PAGE, leftMargin=MARGIN, rightMargin=MARGIN)
    story = [Spacer(1, 0.9 * inch)]
    story.append(_band(title.upper(), f"{ipn}   ·   {kind}" if ipn else kind))
    story.append(Spacer(1, 0.35 * inch))
    if info_rows:
        story.append(_table(info_rows, [1.4 * inch, 3.6 * inch], header=False, size=9))
        story.append(Spacer(1, 0.3 * inch))
    row = _images_row(image_paths, max_h=4.2 * inch, max_w=3.3 * inch)
    if row:
        story.append(row)
    doc.build(story, onFirstPage=partial(_footer, chassis=chassis))


def _front_pdf(path: Path, ctx, chassis, priced, scaling, grand_total) -> None:
    doc = SimpleDocTemplate(str(path), pagesize=PAGE,
                            leftMargin=MARGIN, rightMargin=MARGIN,
                            topMargin=MARGIN, bottomMargin=MARGIN)
    story = []
    _cover(story, ctx, chassis, priced, grand_total, scaling)
    _pricing_sections(story, priced)
    _fab_section(story, ctx, chassis)
    doc.build(story, onFirstPage=partial(_footer, chassis=chassis),
              onLaterPages=partial(_footer, chassis=chassis))


# ------------------------------------------------------------- KiCad layers

def _copper_layers(pcb_path: Path) -> List[str]:
    """Copper + silkscreen layer names from a .kicad_pcb, in plot order."""
    text = pcb_path.read_text(encoding="utf-8", errors="replace")
    m = re.search(r"\(layers\s+(.*?)\n\t\)", text, re.DOTALL)
    if not m:
        return ["F.Cu", "B.Cu", "F.Silkscreen", "B.Silkscreen"]
    names = re.findall(r'\(\d+\s+"([A-Za-z0-9_.]+)"\s+(signal|power|mixed|jumper)', m.group(1))
    copper = [n for n, _ in names]
    ordered = sorted(copper, key=lambda n: (0 if n == "F.Cu" else 2 if n == "B.Cu" else 1, n))
    return ordered + ["F.Silkscreen", "B.Silkscreen"]


def export_board_layers(pcb_path: Path, out_pdf: Path) -> bool:
    """Plot all copper + silkscreen layers to one multipage PDF via KiCad CLI."""
    layers = ",".join(_copper_layers(pcb_path))
    out_pdf.parent.mkdir(parents=True, exist_ok=True)
    cmd = [
        "flatpak", "run", "--command=kicad-cli", "org.kicad.KiCad",
        "pcb", "export", "pdf", str(pcb_path),
        "--mode-multipage", "-l", layers, "-o", str(out_pdf),
    ]
    try:
        result = subprocess.run(cmd, capture_output=True, text=True, timeout=300)
    except FileNotFoundError:
        # Non-flatpak KiCad: try a plain kicad-cli on PATH.
        cmd[0:4] = ["kicad-cli"]
        result = subprocess.run(cmd, capture_output=True, text=True, timeout=300)
    if result.returncode != 0 or not out_pdf.is_file():
        print(f"  layer plot failed for {pcb_path.name}: {result.stderr.strip()[:200]}", file=sys.stderr)
        return False
    return True


# -------------------------------------------------------------------- build

def build(ctx: Context, chassis: str, out_pdf: Path, kicad_layers: bool = True) -> Optional[Path]:
    """Build the full release PDF for one chassis. Returns the output path."""
    pairs = collect_lines(ctx, chassis)
    for ch, line in pairs:
        if not line.internal_pn:
            line.internal_pn = existing_ipn(ctx, ch, line)
    lines = [line for _, line in pairs]
    engine = PricingEngine(cache=ctx.cache, mouser=ctx.mouser, digikey=ctx.digikey,
                           octopart=ctx.octopart, mcmaster=ctx.mcmaster)
    priced = engine.price_lines(lines, refresh=False)
    grand = sum(line_total(e["line"], e["price"].unit_price) for e in priced)
    scaling = [(q, sum(line_total(e["line"], e["price"].unit_price, q) for e in priced))
               for q in (1, 2, 3, 5, 10)]

    def ipn_of(pred):
        return next((l.internal_pn for l in lines if pred(l)), "")

    parts: List[Path] = []
    with tempfile.TemporaryDirectory(dir=out_pdf.parent) as work:
        workdir = Path(work)
        front = workdir / "00_front.pdf"
        _front_pdf(front, ctx, chassis, priced, scaling, grand)
        parts.append(front)

        chassis_dir = ctx.hardware_root / chassis
        boards_dir = chassis_dir / "Boards"
        if boards_dir.is_dir():
            for board_dir in sorted(boards_dir.iterdir()):
                if not board_dir.is_dir():
                    continue
                name = board_dir.name
                sch_pdf = board_dir / f"{name}.pdf"
                pcb = board_dir / f"{name}.kicad_pcb"
                ipn = ipn_of(lambda l: l.type == "pcb" and l.designation == name)
                # Renders live at Boards/<Name>*.png (or legacy Boards/<Name>/).
                imgs = sorted(boards_dir.glob(f"{name}*.png")) + sorted(board_dir.glob(f"{name}*.png"))
                div = workdir / f"div_{name}.pdf"
                _divider_pdf(div, name, ipn, "printed circuit board — schematic and PCB layers",
                             [], imgs, chassis)
                parts.append(div)
                if sch_pdf.is_file():
                    parts.append(sch_pdf)
                if kicad_layers and pcb.is_file():
                    layers_pdf = workdir / f"layers_{name}.pdf"
                    if export_board_layers(pcb, layers_pdf):
                        parts.append(layers_pdf)

        for harness_root in ("Wiring", "Harnesses"):
            harness_dir = chassis_dir / harness_root
            if not harness_dir.is_dir():
                continue
            for part_dir in sorted(harness_dir.iterdir()):
                if not part_dir.is_dir() or part_dir.name.startswith("."):
                    continue
                sch_pdf = part_dir / f"{part_dir.name}.pdf"
                if not sch_pdf.is_file():
                    continue
                ipn = ipn_of(lambda l: l.vendor_hint == "assembly" and l.designation == part_dir.name)
                div = workdir / f"div_{part_dir.name}.pdf"
                _divider_pdf(div, part_dir.name, ipn, "wiring harness — schematic", [], [], chassis)
                parts.append(div)
                parts.append(sch_pdf)

        out_pdf.parent.mkdir(parents=True, exist_ok=True)
        result = subprocess.run(["pdfunite", *[str(p) for p in parts], str(out_pdf)],
                                capture_output=True, text=True)
        if result.returncode != 0:
            print(f"pdfunite failed: {result.stderr.strip()}", file=sys.stderr)
            return None
    return out_pdf
