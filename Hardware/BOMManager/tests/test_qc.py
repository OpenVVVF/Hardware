"""Per-assembly grouping, costs, and QC form generation."""

from bom_manager import qc
from conftest import make_chassis


def test_assembly_groups_and_costs(ctx):
    make_chassis(ctx.hardware_root)
    # Price the mech row before grouping so the aggregated line carries it.
    ctx.db.add("McMaster", "94669A199", {
        "description": "s", "type": "fastener", "mcmaster_part": "94669A199", "manual_price": 3.10,
    })
    groups = qc.assembly_groups(ctx, "Chassis1")
    by_name = {g.name: g for g in groups}
    assert set(by_name) == {"MainBoard", "GDHarness", "HW-C1-PLATE-A", "MechanicalBOM"}
    assert by_name["MainBoard"].kind == "board"
    assert by_name["MainBoard"].items[("r_1210_3225metric|10k")] == 2
    assert by_name["GDHarness"].items[("wire_10awg|wire10awgred")] == 1
    assert by_name["MechanicalBOM"].items[("mcmaster|94669a199")] == 12

    # Cost: mech row priced at 3.10 each -> 12 * 3.10; plate from info.txt -> 2 * 50.71
    costs = {r["name"]: r["cost"] for r in qc.assembly_costs(ctx, groups)}
    assert abs(costs["MechanicalBOM"] - 12 * 3.10) < 1e-9
    assert abs(costs["HW-C1-PLATE-A"] - 2 * 50.71) < 1e-9


def test_qc_forms_build(ctx, tmp_path):
    make_chassis(ctx.hardware_root)
    ctx.descriptor_registry.set("Chassis1", "pcb", "MainBoard", "MAIN")
    out = qc.build_qc_forms(ctx, "Chassis1", tmp_path / "QC.pdf")
    assert out is not None and out.is_file()
    assert out.stat().st_size > 3000
