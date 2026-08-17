"""Shell smoke tests: commands run against a synthetic chassis without crashing."""

from bom_manager.cli import BomShell
from conftest import make_chassis


def _register_descriptors(ctx):
    """Pre-register the descriptors `generate --no-prompt` would otherwise prompt for."""
    ctx.descriptor_registry.set("Chassis1", "pcb", "MainBoard", "MAIN")
    ctx.descriptor_registry.set("Chassis1", "wiring", "GDHarness", "GDH")


def test_shell_commands_smoke(ctx, capsys):
    make_chassis(ctx.hardware_root)
    shell = BomShell(ctx)
    shell.chassis = "Chassis1"
    for line in [
        "status",
        "parts",
        "parts --type wiring",
        "mech",
        "fab",
        "rev list",
        "stock",
        "chassis",
        "boguscommand",
    ]:
        shell.onecmd(line)
        assert not shell.last_error, f"{line!r} failed: {capsys.readouterr().out}"


def test_price_pack_stock_flow(ctx, capsys):
    make_chassis(ctx.hardware_root)
    shell = BomShell(ctx)
    shell.chassis = "Chassis1"
    shell.onecmd("price 94669A199 3.10")
    shell.onecmd("pack 94669A199 25")
    shell.onecmd("stock 94669A199 19")
    assert not shell.last_error
    entry = ctx.db.lookup("McMaster", "94669A199")
    assert entry["manual_price"] == 3.10
    assert entry["pack_size"] == 25
    assert ctx.inventory.get("McMaster", "94669A199") == 19
    # Cache was kept in agreement so reports show the manual price.
    assert ctx.cache.get("mcmaster", "94669A199").unit_price == 3.10


def test_rev_bump_via_shell_with_quoted_note(ctx, capsys):
    make_chassis(ctx.hardware_root)
    _register_descriptors(ctx)
    shell = BomShell(ctx)
    shell.chassis = "Chassis1"
    shell.onecmd("generate --no-prompt --no-pcb-zips --output-dir " + str(ctx.hardware_root / "out"))
    assert not shell.last_error, capsys.readouterr().out
    shell.onecmd('rev bump PLATE --note "widen holes"')
    assert not shell.last_error
    entry = ctx.pn_registry.lookup_by_pn("HW-C1-PLT-PLATE-B")
    assert entry is not None
    assert entry["history"][-1]["note"] == "widen holes"
    info = ctx.hardware_root / "Chassis1" / "Mechanical" / "Fab" / "HW-C1-PLATE-A" / "info.txt"
    assert "Rev=B" in info.read_text()


def test_one_shot_main_generate(ctx, capsys, tmp_path):
    make_chassis(ctx.hardware_root)
    _register_descriptors(ctx)
    # One-shot mode (`python3 bom.py generate ...`) dispatches through the same
    # shell; call it directly to keep the tmp-path context.
    shell = BomShell(ctx)
    shell.chassis = "Chassis1"
    shell.onecmd("generate --no-prompt --no-pcb-zips --output-dir " + str(tmp_path / "out"))
    assert not shell.last_error, capsys.readouterr().out
    assert (tmp_path / "out" / "BOMs" / "Consolidated_BOM.csv").exists()
    assert (tmp_path / "out" / "BOMs" / "assembly_bom.csv").exists()
