"""Harness folder naming conventions are adopted without prompts."""

from bom_manager.bom import BomLine
from bom_manager.generate import descriptor_for_line


def _asm_line(folder_name):
    return BomLine(
        key="k", footprint="WH", designation=folder_name,
        description=folder_name, customer_part="", quantity=1,
        type="wiring", sources=[f"Chassis1/{folder_name}"], vendor_hint="assembly",
    )


def test_ipn_named_folder_with_rev(ctx):
    line = _asm_line("HW-C1-WH-GD-A")
    desc = descriptor_for_line(line, ctx.descriptor_registry, "Chassis1")
    assert desc == "GD"
    assert ctx.descriptor_registry.get("Chassis1", "wiring", "HW-C1-WH-GD-A") == "GD"


def test_ipn_named_folder_without_rev(ctx):
    line = _asm_line("HW-C1-WH-GD")
    desc = descriptor_for_line(line, ctx.descriptor_registry, "Chassis1")
    assert desc == "GD"


def test_ipn_named_folder_longer_descriptor(ctx):
    line = _asm_line("HW-C1-WH-GDCTRL-A")
    desc = descriptor_for_line(line, ctx.descriptor_registry, "Chassis1")
    assert desc == "GDCTRL"
