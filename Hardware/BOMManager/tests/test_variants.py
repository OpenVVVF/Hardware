"""Build-configuration variants: rule matching, pipeline threading, output routing."""

import json

import pytest

from bom_manager import generate
from bom_manager.addpart import collect_items, collect_lines
from bom_manager.variants import Variant, apply_variant, load_chassis_variants
from conftest import make_chassis


def _register_descriptors(ctx):
    """Pre-register the descriptors `generate --no-prompt` would otherwise prompt for."""
    ctx.descriptor_registry.set("Chassis1", "pcb", "MainBoard", "MAIN")
    ctx.descriptor_registry.set("Chassis1", "wiring", "GDHarness", "GDH")


def _run_generate(ctx, *extra):
    return generate.run(["--chassis", "Chassis1", "--no-prompt", "--no-pcb-zips", *extra], ctx)


def _add_plate_b(chassis, qty=3):
    folder = chassis / "Mechanical" / "Fab" / "HW-C1-PLATE-B"
    folder.mkdir(parents=True)
    (folder / "info.txt").write_text(
        f"PartName=HW-C1-PLATE-B\nMaterial=Copper\nQty={qty}\nUnitPrice=12.0\nProcess=Sheet Cutting\n",
        encoding="utf-8",
    )
    return folder


def test_load_chassis_variants(ctx):
    chassis = make_chassis(ctx.hardware_root)
    vset = load_chassis_variants(chassis)
    assert vset.default == "base"
    assert vset.names() == ["base", "alt"]
    assert vset.get("alt").rules
    with pytest.raises(ValueError, match="unknown variant"):
        vset.get("bogus")
    # No variants.yaml -> None (current behavior everywhere).
    assert load_chassis_variants(ctx.hardware_root / "Chassis9") is None


def test_exclude_by_designation_and_source(ctx):
    chassis = make_chassis(ctx.hardware_root)
    _, items_by_chassis, _, _ = collect_items(ctx)
    variant = load_chassis_variants(chassis).get("alt")
    out, changes = apply_variant(items_by_chassis["Chassis1"], variant, chassis_dir=chassis)
    assert all(i.designation != "100nF 100V" for i in out)      # excluded by designation
    assert all(i.source != "HW-C1-PLATE-A" for i in out)        # excluded by source folder
    assert any(i.designation == "220nF 100V" and i.quantity == 1 for i in out)
    actions = {(c.action, c.designation) for c in changes}
    assert ("exclude", "100nF 100V") in actions
    assert ("add", "220nF 100V") in actions


def test_add_rule_flows_through_aggregation(ctx):
    make_chassis(ctx.hardware_root)
    lines = {l.designation: l for _, l in collect_lines(ctx, "Chassis1", variant="alt")}
    assert "100nF 100V" not in lines
    assert "HW-C1-PLATE-A" not in lines
    added = lines["220nF 100V"]
    assert added.quantity == 1
    assert added.type == "capacitor"  # derived from the C_ footprint
    assert added.sources == ["Chassis1/MainBoard"]


def test_unknown_variant_raises(ctx):
    make_chassis(ctx.hardware_root)
    with pytest.raises(ValueError, match="unknown variant"):
        collect_lines(ctx, "Chassis1", variant="bogus")


def test_exclude_matching_nothing_raises(ctx):
    make_chassis(ctx.hardware_root)
    _, items_by_chassis, _, _ = collect_items(ctx)
    variant = Variant("broken", rules=[{"exclude": {"designation": "NO-SUCH-PART"}}])
    with pytest.raises(ValueError, match="matched nothing"):
        apply_variant(items_by_chassis["Chassis1"], variant)


def test_setqty_rule(ctx):
    chassis = make_chassis(ctx.hardware_root)
    _, items_by_chassis, _, _ = collect_items(ctx)
    base = items_by_chassis["Chassis1"]
    base_qty = next(i.quantity for i in base if i.designation == "100nF 100V")
    variant = Variant("v3", rules=[{"setqty": {"designation": "100nF 100V", "quantity": base_qty + 7}}])
    out, changes = apply_variant(base, variant, chassis_dir=chassis)
    adjusted = next(i for i in out if i.designation == "100nF 100V")
    assert adjusted.quantity == base_qty + 7
    assert changes[0].action == "setqty" and changes[0].quantity == 7
    # Items are copied, not mutated in place: the caller's list is untouched.
    assert next(i.quantity for i in base if i.designation == "100nF 100V") == base_qty
    with pytest.raises(ValueError, match="matched nothing"):
        apply_variant(base, Variant("broken", rules=[{"setqty": {"designation": "NOPE", "quantity": 1}}]))
    with pytest.raises(ValueError, match="needs 'quantity'"):
        apply_variant(base, Variant("broken", rules=[{"setqty": {"designation": "100nF 100V"}}]))


def test_add_fab_rule(ctx):
    chassis = make_chassis(ctx.hardware_root)
    _add_plate_b(chassis, qty=3)
    _, items_by_chassis, _, _ = collect_items(ctx)
    variant = Variant("v2", rules=[{"add": {"fab": "HW-C1-PLATE-B", "quantity": 1}}])
    out, changes = apply_variant(items_by_chassis["Chassis1"], variant, chassis_dir=chassis)
    matching = [i for i in out if i.source == "HW-C1-PLATE-B"]
    assert len(matching) == 1                    # replaces the discovered item, no double count
    assert matching[0].quantity == 1             # rule quantity overrides info.txt
    assert matching[0].vendor_hint == "sendcutsend"
    assert changes[0].action == "add"


def test_variant_only_fab_folder_left_out_of_base(ctx):
    """A folder referenced by an add:fab rule is variant-only, not in the base BOM."""
    chassis = make_chassis(ctx.hardware_root)
    _add_plate_b(chassis)
    (chassis / "variants.yaml").write_text(
        "default: base\n"
        "variants:\n"
        "  base:\n"
        "    description: Base build\n"
        "    rules: []\n"
        "  v2:\n"
        "    description: Adds plate B\n"
        "    rules:\n"
        "      - add: {fab: HW-C1-PLATE-B, quantity: 1}\n",
        encoding="utf-8",
    )
    _, items_by_chassis, _, _ = collect_items(ctx)
    assert all(i.source != "HW-C1-PLATE-B" for i in items_by_chassis["Chassis1"])
    lines = {l.designation: l for _, l in collect_lines(ctx, "Chassis1", variant="v2")}
    assert lines["HW-C1-PLATE-B"].quantity == 1


def test_default_variant_output_matches_no_variant(ctx):
    """Empty-rules default variant output is byte-identical to a no-variants run."""
    chassis = make_chassis(ctx.hardware_root)
    _register_descriptors(ctx)
    assert _run_generate(ctx) == 0
    bom = ctx.hardware_root / "Chassis1" / "FabricationData" / "BOMs" / "Consolidated_BOM.csv"
    with_variants = bom.read_bytes()
    (chassis / "variants.yaml").unlink()
    assert _run_generate(ctx) == 0
    assert bom.read_bytes() == with_variants


def test_generate_writes_builds_and_comparison(ctx):
    make_chassis(ctx.hardware_root)
    _register_descriptors(ctx)
    assert _run_generate(ctx) == 0
    fab_dir = ctx.hardware_root / "Chassis1" / "FabricationData"

    # Default variant at root; the alt build under Builds/alt/ with the same file set.
    root_bom = (fab_dir / "BOMs" / "Consolidated_BOM.csv").read_text()
    assert "100nF 100V" in root_bom and "220nF 100V" not in root_bom
    alt_dir = fab_dir / "Builds" / "alt"
    alt_bom = (alt_dir / "BOMs" / "Consolidated_BOM.csv").read_text()
    assert "220nF 100V" in alt_bom
    assert "100nF 100V" not in alt_bom
    assert "HW-C1-PLATE-A" not in alt_bom
    assert (alt_dir / "Pricing_Report.md").exists()
    assert not (alt_dir / "PCB_Fab_Zips").exists()  # boards identical, zips skipped

    # Comparison report + manifest at the root.
    comparison = (fab_dir / "Variant_Comparison.md").read_text()
    assert "Only in alt" in comparison
    assert "Only in base (removed in alt)" in comparison
    manifest = json.loads((fab_dir / "variants.json").read_text())
    assert manifest["chassis"] == "Chassis1" and manifest["default"] == "base"
    by_name = {v["name"]: v for v in manifest["variants"]}
    assert by_name["base"]["is_default"]
    assert by_name["alt"]["output_dir"].endswith("Builds/alt")
    assert by_name["alt"]["added_designations"] == ["220NF 100V"]
    assert "100NF 100V" in by_name["alt"]["removed_designations"]
    assert "HW-C1-PLATE-A" in by_name["alt"]["removed_designations"]
    assert by_name["alt"]["total_usd"] != by_name["base"]["total_usd"]

    # .gitignore: root tracks the comparison files; Builds/<v>/ tracks only Pricing_Report.md.
    root_gitignore = (fab_dir / ".gitignore").read_text()
    assert "!Variant_Comparison.md" in root_gitignore and "!variants.json" in root_gitignore
    builds_gitignore = (alt_dir / ".gitignore").read_text()
    assert "!Pricing_Report.md" in builds_gitignore
    assert "!Variant_Comparison.md" not in builds_gitignore


def test_single_variant_flag_routing(ctx):
    make_chassis(ctx.hardware_root)
    _register_descriptors(ctx)
    assert _run_generate(ctx, "--variant", "alt") == 0
    fab_dir = ctx.hardware_root / "Chassis1" / "FabricationData"
    assert (fab_dir / "Builds" / "alt" / "BOMs" / "Consolidated_BOM.csv").exists()
    # Only one variant generated: no comparison report/manifest, nothing at root.
    assert not (fab_dir / "Variant_Comparison.md").exists()
    assert not (fab_dir / "BOMs" / "Consolidated_BOM.csv").exists()


def test_default_variant_flag_writes_root(ctx):
    make_chassis(ctx.hardware_root)
    _register_descriptors(ctx)
    assert _run_generate(ctx, "--variant", "base") == 0
    fab_dir = ctx.hardware_root / "Chassis1" / "FabricationData"
    assert (fab_dir / "BOMs" / "Consolidated_BOM.csv").exists()
    assert not (fab_dir / "Builds").exists()


def test_unknown_variant_flag_fails(ctx):
    make_chassis(ctx.hardware_root)
    _register_descriptors(ctx)
    assert _run_generate(ctx, "--variant", "bogus") == 1
