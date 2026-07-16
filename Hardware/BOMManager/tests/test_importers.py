"""Vendor import text parsers and price persistence."""

from bom_manager.db import PartDatabase
from bom_manager.importers import persist_prices_to_db
from bom_manager.importers.jlcpcb import parse_jlcpcb_text
from bom_manager.importers.mcmaster import parse_mcmaster_text
from bom_manager.importers.mouser import parse_mouser_text
from bom_manager.importers.sendcutsend import normalize_name, parse_cart


def test_mouser_text_extracts_pn_and_price():
    text = "Cart\n80-C1210C104K1RAC  Cap 100nF  Each: $0.1234\n"
    parts = parse_mouser_text(text)
    assert "80-C1210C104K1RAC" in parts
    assert parts["80-C1210C104K1RAC"]["unit_price"] == 0.1234


def test_mcmaster_text_table_view():
    text = "1  12  Socket Head Screw  94669A199  $3.10  $37.20\n"
    parts = parse_mcmaster_text(text)
    assert parts["94669A199"]["unit_price"] == 3.10


def test_sendcutsend_cart_parse():
    text = (
        "HW-C2-DCLBB-A\n"
        "HW-C2-DCLBB-A.step\n"
        "Sheet Cutting\n"
        "Copper (.187\")\n"
        "12.357 x 0.551 in\n"
        "Bending\n"
        "Each: $50.71\n"
        "Total: $101.42\n"
    )
    parts = parse_cart(text)
    assert len(parts) == 1
    part = parts[0]
    assert part["material"] == "Copper"
    assert part["unit_price"] == 50.71
    assert part["qty"] == 2
    assert "Bending" in part["services"]
    assert normalize_name("HW C2_DCLBB A") == "HW-C2-DCLBB-A"


def test_jlcpcb_text():
    text = "IOBoard_Y6\nPCB prototype: 5 pcs\n5\n$24.50\n"
    parts = parse_jlcpcb_text(text)
    assert parts["IOBoard"]["qty"] == 5
    assert abs(parts["IOBoard"]["unit_price"] - 4.90) < 1e-9


def test_persist_prices_to_db_matches_vendor_field_and_key(tmp_path):
    db = PartDatabase(tmp_path / "db.json")
    db.add("R_1210", "100nF", {
        "description": "cap", "type": "capacitor", "mouser_part": "80-C1210C104K1RAC",
    })
    db.add("McMaster", "94669A199", {
        "description": "94669A199", "type": "mechanical", "mcmaster_part": "94669A199",
    })
    n = persist_prices_to_db(db, "mouser", {"80-C1210C104K1RAC": 0.25})
    assert n == 1
    assert db.lookup("R_1210", "100nF")["manual_price"] == 0.25
    assert db.lookup("R_1210", "100nF")["price_updated"]
    n = persist_prices_to_db(db, "mcmaster", {"94669a199": 3.10})
    assert n == 1
    assert db.lookup("McMaster", "94669A199")["manual_price"] == 3.10
