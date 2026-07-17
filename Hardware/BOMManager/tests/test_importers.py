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


def test_digikey_bom_tool_export(tmp_path):
    from bom_manager.importers.digikey import parse_digikey_file

    csv_path = tmp_path / "digikey_bom.csv"
    csv_path.write_text(
        "Country of Origin may be different at time of shipment.,,,,\n"
        "The HTSUS and ECCN information (if provided),,,,\n"
        "Index,Manufacturer Part Number,Requested Quantity 1,Digi-Key Part Number 1,Unit Price 1,Requested Part Number\n"
        "1,,,,,Digi-Key Part Number\n"
        "2,5586 210 MM X 300 MM X 2.0 MM,1,3M156058-ND,$46.98,\n",
        encoding="utf-8",
    )
    prices, unmatched = parse_digikey_file(csv_path)
    assert prices == {"3M156058-ND": 46.98}
    assert unmatched == ["Digi-Key Part Number"]


def test_mouser_excel_nan_input_falls_back_and_backfills(tmp_path):
    import pandas as pd
    from bom_manager.importers.mouser import backfill_mouser_pns, parse_mouser_excel

    xlsx = tmp_path / "mouser.xlsx"
    pd.DataFrame([
        {"Mouser Part Number (Input)": "80-C1210C104K1RAC", "Mfr Part Number (Input)": "100nF",
         "Mouser Part Number": "80-C1210C104K1RAC", "Order Unit Price (USD)": "$0.16", "Quantity 1": 70},
        {"Mouser Part Number (Input)": None, "Mfr Part Number (Input)": "STM32G474RCT3",
         "Mouser Part Number": "511-STM32G474RCT3", "Order Unit Price (USD)": "$8.31", "Quantity 1": 2},
    ]).to_excel(xlsx, index=False)

    parts = parse_mouser_excel(xlsx)
    assert parts["80-C1210C104K1RAC"]["unit_price"] == 0.16
    assert parts["511-STM32G474RCT3"]["unit_price"] == 8.31
    assert parts["511-STM32G474RCT3"]["mfr_input"] == "STM32G474RCT3"


def test_backfill_mouser_pns(ctx):
    from bom_manager.importers.mouser import backfill_mouser_pns
    from conftest import make_chassis

    make_chassis(ctx.hardware_root)
    # "10k" is a live BOM line with no DB entry; "100nF 100V" gets an entry first.
    ctx.db.add("C_1210_3225Metric", "100nF 100V", {"description": "cap", "type": "capacitor", "mouser_part": ""})
    n = backfill_mouser_pns(ctx, {
        "80-RES10K": {"mfr_input": "10k"},
        "80-CAP100N": {"mfr_input": "100nF 100V"},
        "80-GHOST": {"mfr_input": "not-a-part"},
    })
    assert n == 2
    assert ctx.db.lookup("R_1210_3225Metric", "10k")["mouser_part"] == "80-RES10K"
    assert ctx.db.lookup("C_1210_3225Metric", "100nF 100V")["mouser_part"] == "80-CAP100N"
