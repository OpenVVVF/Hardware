"""End-to-end pipeline on a synthetic chassis: assembly lines, fab status, rev sync."""

from bom_manager import fab
from bom_manager.addpart import collect_items, collect_lines
from conftest import make_chassis


def test_collect_items_adds_pcb_and_harness_assembly_lines(ctx):
    make_chassis(ctx.hardware_root)
    _, items_by_chassis, boards, harnesses = collect_items(ctx)
    assert boards["Chassis1"] == {"MainBoard"}
    assert harnesses["Chassis1"] == {"GDHarness"}
    categories = {i.category for i in items_by_chassis["Chassis1"]}
    assert "pcb_fab" in categories
    assert "harness_asm" in categories


def test_harness_assembly_line_types(ctx):
    make_chassis(ctx.hardware_root)
    lines = dict(((l.designation, l) for _, l in collect_lines(ctx)))
    asm = lines["GDHarness"]
    assert asm.type == "wiring"
    assert asm.primary_vendor() == "assembly"
    # Wire component is wiring; connector is not lumped into wiring.
    assert lines["WIRE 10AWG RED"].type == "wiring"
    assert lines["0039303040"].type != "wiring"
    # Board and mech parts aggregate as before.
    assert lines["10k"].type == "resistor"
    assert lines["94669A199"].type == "mechanical"


def test_fab_collect_and_problems(ctx):
    chassis = make_chassis(ctx.hardware_root)
    status = fab.collect(ctx, "Chassis1")
    assert len(status.boards) == 1
    assert status.boards[0].bom_csv
    assert status.boards[0].zip_path is None  # generate not run yet
    assert any("no fab price" in p for p in status.problems)

    assert len(status.parts) == 1
    part = status.parts[0]
    assert part.has_step and part.has_info and part.price == 50.71 and part.qty == 2

    assert len(status.harnesses) == 1 and status.harnesses[0].has_csv

    # Break the fab part: remove info.txt -> loudly not ready.
    (chassis / "Mechanical" / "Fab" / "HW-C1-PLATE-A" / "info.txt").unlink()
    status = fab.collect(ctx, "Chassis1")
    assert not status.parts[0].ready
    assert any("info.txt missing" in p for p in status.problems)


def test_rev_sync_writes_info_txt(ctx):
    chassis = make_chassis(ctx.hardware_root)
    path = fab.sync_info_rev(ctx, "c1|plate|fab:hw-c1-plate-a", "B")
    assert path is not None
    text = path.read_text()
    assert "Rev=B" in text
    # Folder is untouched.
    assert (chassis / "Mechanical" / "Fab" / "HW-C1-PLATE-A").is_dir()
    # Second sync replaces rather than duplicates.
    fab.sync_info_rev(ctx, "c1|plate|fab:hw-c1-plate-a", "C")
    assert path.read_text().count("Rev=") == 1
    assert "Rev=C" in path.read_text()


def test_harness_per_chassis_qty(ctx):
    """info.txt Qty=N scales both the assembly line and the components."""
    chassis = make_chassis(ctx.hardware_root)
    (chassis / "Harnesses" / "GDHarness" / "info.txt").write_text("Qty=4\n", encoding="utf-8")
    lines = dict(((l.designation, l) for _, l in collect_lines(ctx)))
    assert lines["GDHarness"].quantity == 4        # assembly line
    assert lines["WIRE 10AWG RED"].quantity == 4   # component x4
    assert lines["0039303040"].quantity == 8       # component x4 of 2
