"""Release PDF report: layer parsing and end-to-end build (sans KiCad CLI)."""

from bom_manager import pdfreport
from conftest import make_chassis

FAKE_PCB = """(kicad_pcb (version 20240108) (generator "pcbnew")
\t(layers
\t\t(0 "F.Cu" signal)
\t\t(1 "In1.Cu" signal)
\t\t(2 "In2.Cu" signal)
\t\t(31 "B.Cu" signal)
\t\t(36 "B.Silkscreen" user)
\t)
)
"""


def test_copper_layers_order(tmp_path):
    pcb = tmp_path / "b.kicad_pcb"
    pcb.write_text(FAKE_PCB, encoding="utf-8")
    layers = pdfreport._copper_layers(pcb)
    assert layers == ["F.Cu", "In1.Cu", "In2.Cu", "B.Cu", "F.Silkscreen", "B.Silkscreen"]


def test_build_pdf_without_kicad(ctx, tmp_path):
    make_chassis(ctx.hardware_root)
    ctx.descriptor_registry.set("Chassis1", "pcb", "MainBoard", "MAIN")
    ctx.descriptor_registry.set("Chassis1", "wiring", "GDHarness", "GDH")
    out = tmp_path / "Release.pdf"
    result = pdfreport.build(ctx, "Chassis1", out, kicad_layers=False)
    assert result == out
    assert out.is_file() and out.stat().st_size > 5000
    text = out.read_bytes()
    assert text.startswith(b"%PDF")
    # Front matter + fab part page + board/harness dividers merged: >1 page.
    assert text.count(b"/Type /Page") > 1
