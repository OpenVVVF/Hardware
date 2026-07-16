"""Scaffolding new fab parts, harnesses, and boards."""

from bom_manager import scaffold
from conftest import make_chassis


def test_new_fab_part(ctx):
    make_chassis(ctx.hardware_root)
    folder = scaffold.new_fab_part(ctx, "Chassis1", "Test Bracket")
    assert folder.name == "HW-C1-TEST-BRACKET-A"
    info = (folder / "info.txt").read_text()
    assert "PartName=Test Bracket" in info
    assert (folder / "README.md").exists()
    # Second call refuses to clobber.
    assert scaffold.new_fab_part(ctx, "Chassis1", "Test Bracket") is None


def test_new_harness_preregisters_descriptor(ctx):
    make_chassis(ctx.hardware_root)
    folder = scaffold.new_harness(ctx, "Chassis1", "PDBHarness")
    assert folder.is_dir()
    assert ctx.descriptor_registry.get("Chassis1", "wiring", "PDBHarness") == "PDBHARNESS"
    readme = (folder / "README.md").read_text()
    assert "Generate BOM" in readme


def test_new_board_preregisters_descriptor(ctx):
    make_chassis(ctx.hardware_root)
    board = scaffold.new_board(ctx, "Chassis1", "PowerBoard", descriptor="PWR")
    assert (board / "Fab").is_dir()
    assert ctx.descriptor_registry.get("Chassis1", "pcb", "PowerBoard") == "PWR"
    assert "HW-C1-PCB-PWR-A" in (board / "README.md").read_text()
