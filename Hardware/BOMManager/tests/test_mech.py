"""MechanicalBOM.txt management."""

from bom_manager import mech
from bom_manager.db import PartDatabase


def _write(path, text):
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(text, encoding="utf-8")


def test_round_trip_canonical_format(tmp_path):
    path = tmp_path / "MechanicalBOM.txt"
    _write(path, "Qty, Vendor, PN\n12, McMaster, 94669A199\n3, Digikey, 3M156058-ND\n")
    rows = mech.load(path)
    assert len(rows) == 2
    mech.save(path, rows)
    text = path.read_text()
    assert text.splitlines()[0] == "Qty,Vendor,PN,Description"
    # Sorted by vendor then PN: Digikey before McMaster.
    assert text.splitlines()[1].startswith("3,Digikey")
    rows2 = mech.load(path)
    key = lambda r: (r.vendor.lower(), r.pn.lower())
    assert sorted(map(key, rows2)) == sorted(map(key, rows))


def test_upsert_creates_and_updates_case_insensitively(tmp_path):
    rows = []
    row, created = mech.upsert(rows, "McMaster", "94669A199", 12, "screw")
    assert created and row.qty == 12
    row, created = mech.upsert(rows, "mcmaster", "94669a199", 20)
    assert not created
    assert len(rows) == 1 and rows[0].qty == 20 and rows[0].description == "screw"


def test_set_qty_and_remove_need_unique_match(tmp_path):
    rows = [mech.MechRow(1, "McMaster", "AAA111"), mech.MechRow(2, "McMaster", "BBB222")]
    assert mech.set_qty(rows, "aaa111", 5).qty == 5
    assert mech.set_qty(rows, "AA", 5).pn == "AAA111"  # unique substring match
    rows.append(mech.MechRow(3, "McMaster", "AAA999"))
    assert mech.set_qty(rows, "AAA", 5) is None  # ambiguous
    removed = mech.remove(rows, "BBB222")
    assert removed.pn == "BBB222" and len(rows) == 2
    assert mech.remove(rows, "NOPE") is None


def test_ensure_db_entry_seeds_vendor_field(tmp_path):
    db = PartDatabase(tmp_path / "db.json")
    row = mech.MechRow(12, "McMaster", "94669A199", "screw")
    assert mech.ensure_db_entry(db, row)
    entry = db.lookup("McMaster", "94669A199")
    assert entry["mcmaster_part"] == "94669A199"
    assert entry["description"] == "94669A199"  # identity-stable, see docstring
    assert not mech.ensure_db_entry(db, row)  # second call is a no-op
