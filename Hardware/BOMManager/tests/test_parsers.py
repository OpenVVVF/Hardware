"""BOM source parsers."""

from pathlib import Path

from bom_manager.discover import BomSource, discover_boms
from bom_manager.parsers import parse_source


def test_kicad_board_csv(tmp_path):
    csv_path = tmp_path / "MainBoard.csv"
    csv_path.write_text(
        "Id;Designator;Footprint;Quantity;Designation;Supplier and ref\n"
        '1;"R1,R2";R_1210_3225Metric;2;10k;\n',
        encoding="utf-8",
    )
    src = BomSource("Chassis1", "board", "MainBoard", csv_path)
    items = list(parse_source(src))
    assert len(items) == 1
    assert items[0].quantity == 2
    assert items[0].designation == "10k"
    assert items[0].category == "board"


def test_mechanical_csv(tmp_path):
    path = tmp_path / "MechanicalBOM.txt"
    path.write_text("Qty,Vendor,PN\n12,McMaster,94669A199\n", encoding="utf-8")
    src = BomSource("Chassis1", "mechanical", "MechanicalBOM", path, vendor_hint="mcmaster")
    items = list(parse_source(src))
    assert len(items) == 1
    assert items[0].designation == "94669A199"
    assert items[0].vendor_hint == "mcmaster"


def test_sendcutsend_folder(tmp_path):
    folder = tmp_path / "HW-C1-PLATE-A"
    folder.mkdir()
    (folder / "HW-C1-PLATE-A.step").write_text("x", encoding="utf-8")
    (folder / "info.txt").write_text(
        "PartName=HW-C1-PLATE-A\nMaterial=Copper\nQty=2\nUnitPrice=50.71\n", encoding="utf-8"
    )
    src = BomSource("Chassis1", "sendcutsend_folder", folder.name, folder)
    items = list(parse_source(src))
    assert len(items) == 1
    assert items[0].quantity == 2
    assert items[0].step_path.name == "HW-C1-PLATE-A.step"
    assert items[0].metadata["UnitPrice"] == "50.71"


def test_harness_csv_gets_harness_category(tmp_path):
    csv_path = tmp_path / "GDHarness.csv"
    csv_path.write_text(
        "Id;Designator;Footprint;Quantity;Designation;Supplier and ref\n"
        '1;"W1";Wire_10AWG;1;WIRE 10AWG RED;\n',
        encoding="utf-8",
    )
    src = BomSource("Chassis1", "harness", "GDHarness", csv_path)
    items = list(parse_source(src))
    assert items[0].category == "harness"
    assert items[0].source == "GDHarness"


def test_kicad10_symbol_fields_export(tmp_path):
    """KiCad 10 schematic export (Reference,Qty,Value,DNP,Exclude from BOM,...)."""
    csv_path = tmp_path / "TempSenseHarness.csv"
    csv_path.write_text(
        '"Reference","Qty","Value","DNP","Exclude from BOM","Exclude from Board","Footprint","Datasheet"\n'
        '"J1","1","455-2266-ND","","","","",""\n'
        '"Z1,Z2","2","SXH-001T-P0.6","","","","",""\n'
        '"R9","1","10k","DNP","","","R_0805",""\n'
        '"R10","1","4.7k","","yes","","R_0805",""\n',
        encoding="utf-8",
    )
    src = BomSource("Chassis1", "harness", "TempSenseHarness", csv_path)
    items = list(parse_source(src))
    assert [i.designation for i in items] == ["455-2266-ND", "SXH-001T-P0.6"]
    assert items[1].quantity == 2
    assert items[1].designators == "Z1,Z2"


def test_discover_finds_harnesses(tmp_path):
    from conftest import make_chassis

    hw = tmp_path / "Hardware"
    hw.mkdir()
    make_chassis(hw)
    # User-style layout: Wiring/<IPN>/Fab/<Name>.csv
    fab = hw / "Chassis1" / "Wiring" / "HW-C1-WH-X-A" / "Fab"
    fab.mkdir(parents=True)
    (fab / "HW-C1-WH-X-A.csv").write_text(
        "Id;Designator;Footprint;Quantity;Designation;Supplier and ref\n"
        '1;"J1";Connector_X;1;524265-E;\n',
        encoding="utf-8",
    )
    sources = list(discover_boms(hw))
    categories = {s.category for s in sources}
    assert "board" in categories
    assert "mechanical" in categories
    assert "sendcutsend_folder" in categories
    assert "harness" in categories
    harness_sources = [s for s in sources if s.category == "harness"]
    assert {s.board for s in harness_sources} == {"GDHarness", "HW-C1-WH-X-A"}
