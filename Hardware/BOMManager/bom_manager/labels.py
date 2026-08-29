"""Printable labels: high-voltage warnings and per-assembly part labels.

Output: FabricationData/Labels.pdf — a page of HV warning labels, a chassis
ID label, and a grid of per-assembly labels (name, IPN, rev, qty) in the
InverterGen5 C2 convention.
"""

from functools import partial
from pathlib import Path
from typing import Optional

from reportlab.lib import colors
from reportlab.lib.units import inch, mm
from reportlab.platypus import PageBreak, Paragraph, SimpleDocTemplate, Spacer, Table, TableStyle

from . import qc
from .context import Context
from .pdfreport import (
    ACCENT, ACCENT_LIGHT, CONTENT_W, GREY, MARGIN, PAGE, _footer, _para, _rule,
)

BRAND = "InverterGen5 C2"
HV_YELLOW = colors.HexColor("#F5C518")


def _hv_label(width):
    inner = [
        [_para("HIGH VOLTAGE", size=22, bold=True, align=1)],
        [_para("DC LINK BUS — UP TO 350 VDC", size=12, bold=True, align=1)],
        [_para("800 V-class design. The capacitor bank holds charge for hours after power-down. "
               "Verify bus voltage with a meter and discharge through a power resistor before service.",
               size=9, align=1)],
        [_para(f"{BRAND} · UC Santa Cruz — Corzine Lab", size=8, color=GREY, align=1)],
    ]
    t = Table(inner, colWidths=[width])
    t.setStyle(TableStyle([
        ("BACKGROUND", (0, 0), (0, 0), HV_YELLOW),
        ("BACKGROUND", (0, 1), (0, 1), HV_YELLOW),
        ("BOX", (0, 0), (-1, -1), 2.2, colors.black),
        ("TOPPADDING", (0, 0), (-1, -1), 4 * mm),
        ("BOTTOMPADDING", (0, 0), (-1, -1), 4 * mm),
        ("LEFTPADDING", (0, 0), (-1, -1), 5 * mm),
        ("RIGHTPADDING", (0, 0), (-1, -1), 5 * mm),
    ]))
    return t


def _part_label(g, width):
    inner = [
        [_para(BRAND, size=7, color=GREY, bold=True)],
        [_para(g.title or g.name, size=12, bold=True)],
        [_para(f"{g.ipn or '—'}  ·  Rev {g.rev or 'A'}  ·  qty {g.qty_per_chassis}", size=9)],
    ]
    t = Table(inner, colWidths=[width])
    t.setStyle(TableStyle([
        ("BOX", (0, 0), (-1, -1), 1.0, ACCENT),
        ("BACKGROUND", (0, 0), (-1, -1), colors.white),
        ("BACKGROUND", (0, 0), (0, 0), ACCENT_LIGHT),
        ("TOPPADDING", (0, 0), (-1, -1), 2.5 * mm),
        ("BOTTOMPADDING", (0, 0), (-1, -1), 2.5 * mm),
        ("LEFTPADDING", (0, 0), (-1, -1), 4 * mm),
    ]))
    return t


def _chassis_label(width):
    inner = [
        [_para(BRAND, size=8, color=GREY, bold=True)],
        [_para("Traction Inverter — Chassis 2", size=14, bold=True)],
        [_para("Serial No.  ______________", size=10)],
        [_para("Assembled:  ______________      Inspected:  ______________", size=10)],
    ]
    t = Table(inner, colWidths=[width])
    t.setStyle(TableStyle([
        ("BOX", (0, 0), (-1, -1), 1.4, colors.black),
        ("BACKGROUND", (0, 0), (0, 0), ACCENT_LIGHT),
        ("TOPPADDING", (0, 0), (-1, -1), 3 * mm),
        ("BOTTOMPADDING", (0, 0), (-1, -1), 3 * mm),
        ("LEFTPADDING", (0, 0), (-1, -1), 4 * mm),
    ]))
    return t


def build(ctx: Context, chassis: str, out_pdf: Path, variant=None) -> Optional[Path]:
    groups = qc.assembly_groups(ctx, chassis, variant=variant)
    story = []

    story.append(_para("Warning Labels", size=16, bold=True))
    story.extend(_rule())
    story.append(Spacer(1, 0.1 * inch))
    story.append(_hv_label(CONTENT_W * 0.85))
    story.append(Spacer(1, 0.35 * inch))
    story.append(_hv_label(CONTENT_W * 0.85))
    story.append(Spacer(1, 0.4 * inch))
    story.append(_para("Chassis ID", size=14, bold=True))
    story.extend(_rule())
    story.append(Spacer(1, 0.1 * inch))
    story.append(_chassis_label(CONTENT_W * 0.7))
    story.append(PageBreak())

    story.append(_para("Assembly Labels", size=16, bold=True))
    story.extend(_rule())
    story.append(Spacer(1, 0.12 * inch))
    col_w = CONTENT_W * 0.46
    cells, row = [], []
    for g in groups:
        row.append(_part_label(g, col_w))
        if len(row) == 2:
            cells.append(row)
            row = []
    if row:
        row.append("")
        cells.append(row)
    grid = Table(cells, colWidths=[CONTENT_W * 0.48, CONTENT_W * 0.52])
    grid.setStyle(TableStyle([("VALIGN", (0, 0), (-1, -1), "TOP"),
                              ("TOPPADDING", (0, 0), (-1, -1), 3 * mm)]))
    story.append(grid)

    out_pdf.parent.mkdir(parents=True, exist_ok=True)
    doc = SimpleDocTemplate(str(out_pdf), pagesize=PAGE, leftMargin=MARGIN, rightMargin=MARGIN)
    doc.build(story, onFirstPage=partial(_footer, chassis=chassis),
              onLaterPages=partial(_footer, chassis=chassis))
    return out_pdf
