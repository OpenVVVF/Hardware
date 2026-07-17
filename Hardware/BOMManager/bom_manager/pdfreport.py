"""Full release PDF report: cover, pricing, fabrication package, and per-board /
per-harness sections with divider pages, schematics, and PCB layer plots.

Generated pages are built with reportlab; schematic and layer PDFs are merged
in order with pdfunite. Board layer plots and 3D board renders come from KiCad
CLI (flatpak); fab-part previews are rendered from STEP with cadquery +
matplotlib. Page style follows the project docs (HARA): A4, Liberation Sans,
green accent with ruled section headers.
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
from reportlab.lib.pagesizes import A4
from reportlab.lib.styles import getSampleStyleSheet
from reportlab.lib.units import inch, mm
from reportlab.pdfbase import pdfmetrics
from reportlab.pdfbase.ttfonts import TTFont
from reportlab.platypus import (
    HRFlowable, Image, PageBreak, Paragraph, SimpleDocTemplate, Spacer, Table,
    TableStyle,
)

from . import docgen, fab, render
from .addpart import collect_lines, existing_ipn, known_price
from .context import Context
from .pricing import PricingEngine, line_total

PAGE = A4
MARGIN = 18 * mm
CONTENT_W = PAGE[0] - 2 * MARGIN
STYLES = getSampleStyleSheet()

_FONT_DIR = Path("/usr/share/fonts/truetype/liberation")
pdfmetrics.registerFont(TTFont("LiberationSans", str(_FONT_DIR / "LiberationSans-Regular.ttf")))
pdfmetrics.registerFont(TTFont("LiberationSans-Bold", str(_FONT_DIR / "LiberationSans-Bold.ttf")))
pdfmetrics.registerFont(TTFont("LiberationSans-Italic", str(_FONT_DIR / "LiberationSans-Italic.ttf")))
pdfmetrics.registerFont(TTFont("LiberationSans-BoldItalic", str(_FONT_DIR / "LiberationSans-BoldItalic.ttf")))
pdfmetrics.registerFontFamily(
    "LiberationSans", normal="LiberationSans", bold="LiberationSans-Bold",
    italic="LiberationSans-Italic", boldItalic="LiberationSans-BoldItalic",
)
BASE_FONT = "LiberationSans"

ACCENT = colors.HexColor("#1E4D2B")
ACCENT_LIGHT = colors.HexColor("#EDF3EE")
ACCENT_PALE = colors.HexColor("#C9DCCD")
GREY = colors.HexColor("#4A4A4A")
GRID = colors.HexColor("#B9C2B9")

VENDOR_DISPLAY = {
    "mcmaster": "McMaster-Carr",
    "mouser": "Mouser",
    "digikey": "Digi-Key",
    "sendcutsend": "SendCutSend",
    "pcb": "PCB Fabrication",
    "assembly": "In-House Assembly",
    "unknown": "Unknown / Missing",
}


def _para(text, style="Normal", size=9, color=None, bold=False, markup=False, align=None):
    st = STYLES[style].clone(f"p{size}{style}{color}{bold}")
    st.fontName = "LiberationSans-Bold" if bold else BASE_FONT
    st.fontSize = size
    st.leading = size + 2.5
    if color is not None:
        st.textColor = color
    if align is not None:
        st.alignment = align
    if not markup:
        text = str(text).replace("&", "&amp;").replace("<", "&lt;").replace(">", "&gt;")
    return Paragraph(str(text), st)


def _rule(space_before=2, space_after=8, thickness=1.1, color=ACCENT):
    return [
        Spacer(1, space_before),
        HRFlowable(width="100%", thickness=thickness, color=color),
        Spacer(1, space_after),
    ]


def _section_header(story, text):
    story.append(_para(text, size=14, bold=True, style="Heading1"))
    story.extend(_rule())


def _table(rows, widths, header=True, size=8):
    header_row = [[_para(c, size=size, color=colors.white, bold=True) for c in rows[0]]] if header else []
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
    canvas.setFont(BASE_FONT, 8)
    canvas.setFillColor(GREY)
    canvas.drawString(MARGIN, 10 * mm, f"Inverter Gen5 — {chassis} Hardware Release")
    canvas.drawCentredString(PAGE[0] / 2, 10 * mm, "")
    canvas.drawRightString(PAGE[0] - MARGIN, 10 * mm, f"{doc.page}")
    canvas.restoreState()


def _draw_swoosh(canvas, doc):
    """HARA-style green arc filling the right side of the cover."""
    w, h = PAGE
    canvas.saveState()
    canvas.setFillColor(ACCENT)
    p = canvas.beginPath()
    p.moveTo(w, h)
    p.lineTo(w, 0)
    p.lineTo(w * 0.60, 0)
    p.curveTo(w * 0.66, h * 0.30, w * 0.80, h * 0.52, w, h * 0.66)
    p.close()
    canvas.drawPath(p, fill=1, stroke=0)
    canvas.restoreState()


# ---------------------------------------------------------------- front matter

def _cover(story, ctx, chassis, priced, grand_total, scaling):
    priced_count = sum(1 for e in priced if e["price"].unit_price is not None)
    story.append(Spacer(1, 1.1 * inch))
    story.append(_para("HARDWARE RELEASE REPORT", size=10, color=ACCENT, bold=True))
    story.append(Spacer(1, 0.16 * inch))
    story.append(_para("Traction Inverter", size=26, bold=True, style="Title"))
    story.append(Spacer(1, 0.42 * inch))
    details = [
        ("Chassis:", chassis),
        ("BOM lines:", f"{len(priced)} ({priced_count} priced)"),
        ("Generated:", datetime.now().date().isoformat()),
    ]
    for label, value in details:
        story.append(Spacer(1, 2))
        story.append(_para(f"<b>{label}</b>  {value}", size=10, markup=True))
    story.append(Spacer(1, 0.5 * inch))
    story.append(_para("Cost at quantity", size=11, bold=True))
    story.extend(_rule(space_after=6))
    rows = [["Units", "Estimated total"]] + [[str(q), f"${t:,.2f}"] for q, t in scaling]
    story.append(_table(rows, [32 * mm, 48 * mm], size=10))
    story.append(Spacer(1, 2.2 * inch))
    story.extend(_rule(thickness=1.4, space_after=4))
    story.append(_para("University of California, Santa Cruz — Corzine Lab", size=8, color=GREY))
    story.append(PageBreak())


def _toc(story, chassis, boards, harnesses, documents=None):
    _section_header(story, "Contents")
    entries = ["Order Summary", "Fabrication Package"]
    entries += [f"{b} — schematic & PCB layers" for b in boards]
    entries += [f"{h} — wiring harness" for h in harnesses]
    if documents:
        entries += [f"{t} — design document" for t in documents]
    for i, e in enumerate(entries, 1):
        story.append(Spacer(1, 3))
        story.append(_para(f"{i}.  {e}", size=10))
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


def _assembly_cost_section(story, ctx, chassis):
    from . import qc
    rows_data = qc.assembly_costs(ctx, qc.assembly_groups(ctx, chassis))
    if not rows_data:
        return
    _section_header(story, "Cost by Assembly")
    rows = [["Assembly", "Kind", "Qty/chassis", "Parts", "Cost (1 unit)"]]
    total = 0.0
    for r in rows_data:
        total += r["cost"]
        rows.append([r["name"], r["kind"], str(r["qty"]), str(r["parts"]), f"${r['cost']:,.2f}"])
    rows.append(["", "", "", "Total", f"${total:,.2f}"])
    story.append(_table(rows, [48 * mm, 34 * mm, 22 * mm, 16 * mm, 30 * mm]))
    story.append(Spacer(1, 0.1 * inch))
    story.append(_para("Costs are per-piece at current prices (pack prices divided out); assembly labor and PCB fab quantities at other build counts follow the pack math in the vendor sections.", size=8, color=GREY))
    story.append(PageBreak())


def _fab_section(story, ctx, chassis, render_dir=None):
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
        rows.append([h.name, "harness", str(h.qty), h.rev or "A", "-", "yes" if h.has_csv else "NO CSV"])
    story.append(_table(rows, [1.9 * inch, 0.8 * inch, 0.5 * inch, 0.5 * inch, 0.9 * inch, 0.7 * inch]))
    problems = status.problems
    if problems:
        story.append(Spacer(1, 0.1 * inch))
        story.append(_para("<b>Not ready:</b> " + "; ".join(problems), size=9, style="Italic", markup=True))
    story.append(PageBreak())

    for p in status.parts:
        if not p.has_info:
            continue
        story.append(_para(p.name, size=13, bold=True, color=ACCENT, style="Heading2"))
        story.extend(_rule(thickness=0.8, space_after=6))
        story.append(_para(p.spec or "-", size=9, color=GREY))
        story.append(Spacer(1, 0.08 * inch))
        # Prefer a fresh STEP render; fall back to the part's info.png.
        imgs = []
        if render_dir is not None:
            steps = sorted(p.folder.glob("*.step")) + sorted(p.folder.glob("*.stp"))
            if steps:
                info = fab._read_info(p.folder / "info.txt")
                preview = render_dir / f"preview_{p.name}.png"
                if render.render_step(steps[0], preview, material=info.get("Material", "")):
                    imgs.append(preview)
        if not imgs:
            imgs = sorted(p.folder.glob("*.png")) + sorted(p.folder.glob("*.jpg"))
        row = _images_row(imgs[:3])
        if row:
            story.append(row)
        story.append(PageBreak())


# ------------------------------------------------------------------- sections

def _divider_pdf(path: Path, title: str, ipn: str, kind: str, info_rows, image_paths: List[Path], chassis: str) -> None:
    doc = SimpleDocTemplate(str(path), pagesize=PAGE, leftMargin=MARGIN, rightMargin=MARGIN)
    story = [Spacer(1, 0.9 * inch)]
    story.append(_para(title.upper(), size=22, bold=True, color=ACCENT))
    story.extend(_rule(thickness=1.4))
    story.append(Spacer(1, 0.1 * inch))
    story.append(_para(f"{ipn}   ·   {kind}" if ipn else kind, size=10, color=GREY))
    story.append(Spacer(1, 0.3 * inch))
    if info_rows:
        story.append(_table(info_rows, [40 * mm, 100 * mm], header=False, size=9))
        story.append(Spacer(1, 0.25 * inch))
    if len(image_paths) > 1:
        # Stack renders full-width so board details stay readable.
        for p in image_paths[:2]:
            row = _images_row([p], max_h=3.2 * inch, max_w=CONTENT_W * 0.92)
            if row:
                story.append(row)
                story.append(Spacer(1, 0.15 * inch))
    else:
        row = _images_row(image_paths, max_h=4.2 * inch, max_w=CONTENT_W * 0.92)
        if row:
            story.append(row)
    doc.build(story, onFirstPage=partial(_footer, chassis=chassis))


def _front_pdf(path: Path, ctx, chassis, priced, scaling, grand_total, boards, harnesses, documents=None, render_dir=None) -> None:
    doc = SimpleDocTemplate(str(path), pagesize=PAGE,
                            leftMargin=MARGIN, rightMargin=MARGIN,
                            topMargin=MARGIN, bottomMargin=MARGIN)
    story = []
    _cover(story, ctx, chassis, priced, grand_total, scaling)
    _toc(story, chassis, boards, harnesses, documents=documents)
    _pricing_sections(story, priced)
    _assembly_cost_section(story, ctx, chassis)
    _fab_section(story, ctx, chassis, render_dir=render_dir)
    doc.build(story, onFirstPage=_draw_swoosh,
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


# -------------------------------------------------------------------- DRC

def _drc_pdf(path: Path, ctx, chassis: str, board: str, max_lines: int = 14) -> bool:
    """One DRC summary page for a board: error count + first findings.
    Warnings are suppressed in the underlying report."""
    report = ctx.hardware_root / chassis / "FabricationData" / "DRC" / f"{board}.txt"
    if not report.is_file():
        return False
    text = report.read_text(encoding="utf-8", errors="replace")
    m = re.search(r"Found (\d+) DRC violations", text)
    errors = int(m.group(1)) if m else 0
    findings = []
    for line in text.splitlines():
        line = line.strip()
        if line.startswith("[") and "]" in line:
            findings.append(re.sub(r"\s+", " ", line))
    unconnected = re.search(r"Found (\d+) unconnected", text)

    doc = SimpleDocTemplate(str(path), pagesize=PAGE, leftMargin=MARGIN, rightMargin=MARGIN)
    story = []
    story.append(_para(f"DRC — {board}", size=14, bold=True, style="Heading1"))
    story.extend(_rule())
    if errors == 0:
        story.append(_para("No DRC errors (warnings suppressed).", size=10))
    else:
        story.append(_para(f"<b>{errors} error{'s' if errors != 1 else ''}</b> (warnings suppressed; full report: FabricationData/DRC/{board}.txt)", size=10, markup=True))
        story.append(Spacer(1, 0.12 * inch))
        for f in findings[:max_lines]:
            story.append(Spacer(1, 2))
            story.append(_para(f, size=8, color=GREY))
        if len(findings) > max_lines:
            story.append(Spacer(1, 4))
            story.append(_para(f"… and {len(findings) - max_lines} more.", size=8, color=GREY))
    if unconnected and int(unconnected.group(1)) > 0:
        story.append(Spacer(1, 0.12 * inch))
        story.append(_para(f"Unconnected items: {unconnected.group(1)}", size=9))
    doc.build(story, onFirstPage=partial(_footer, chassis=chassis))
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
    chassis_dir = ctx.hardware_root / chassis
    boards_dir = chassis_dir / "Boards"
    board_names = []
    if boards_dir.is_dir():
        for d in sorted(boards_dir.iterdir()):
            if not d.is_dir():
                continue
            # A real board has a KiCad project (or at least a schematic PDF);
            # skip stray export dirs like Boards/BOMs/.
            if (d / f"{d.name}.kicad_pcb").is_file() or (d / f"{d.name}.pdf").is_file():
                board_names.append(d.name)
    harness_names = []
    for harness_root in ("Wiring", "Harnesses"):
        harness_dir = chassis_dir / harness_root
        if harness_dir.is_dir():
            harness_names += sorted(
                d.name for d in harness_dir.iterdir()
                if d.is_dir() and not d.name.startswith(".")
            )

    docs = docgen.collect_documents(ctx, chassis)
    if docs:
        ctx.pn_registry.save()

    with tempfile.TemporaryDirectory(dir=out_pdf.parent) as work:
        workdir = Path(work)
        front = workdir / "00_front.pdf"
        _front_pdf(front, ctx, chassis, priced, scaling, grand, board_names, harness_names,
                   documents=[d.title for d in docs], render_dir=workdir)
        parts.append(front)

        if boards_dir.is_dir():
            for name in board_names:
                board_dir = boards_dir / name
                sch_pdf = board_dir / f"{name}.pdf"
                pcb = board_dir / f"{name}.kicad_pcb"
                ipn = ipn_of(lambda l: l.type == "pcb" and l.designation == name)
                # Fresh 3D renders via KiCad CLI; static <Name>*.png as fallback/extra.
                imgs = []
                if pcb.is_file():
                    for side in ("top", "bottom"):
                        shot = workdir / f"render_{name}_{side}.png"
                        if render.render_board_3d(pcb, shot, side=side):
                            imgs.append(shot)
                if not imgs:
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
                drc_pdf = workdir / f"drc_{name}.pdf"
                if _drc_pdf(drc_pdf, ctx, chassis, name):
                    parts.append(drc_pdf)

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

        # Design documents ship in the package: divider + compiled PDF each.
        if docs:
            sec_div = workdir / "div_documents.pdf"
            _divider_pdf(sec_div, "Design Documents", "",
                         "project documentation — safety, security, manual, analyses",
                         [], [], chassis)
            parts.append(sec_div)
            for info in docs:
                div = workdir / f"div_doc_{info.slug}.pdf"
                _divider_pdf(div, info.title, info.ipn, f"design document — rev {info.rev}",
                             [], [], chassis)
                parts.append(div)
                compiled = workdir / f"doc_{info.slug}.pdf"
                if docgen.compile_document(info, compiled):
                    parts.append(compiled)

        out_pdf.parent.mkdir(parents=True, exist_ok=True)
        result = subprocess.run(["pdfunite", *[str(p) for p in parts], str(out_pdf)],
                                capture_output=True, text=True)
        if result.returncode != 0:
            print(f"pdfunite failed: {result.stderr.strip()}", file=sys.stderr)
            return None
    return out_pdf
