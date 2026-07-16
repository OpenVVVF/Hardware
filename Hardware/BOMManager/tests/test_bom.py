"""Batch order math and BOM aggregation."""

from bom_manager.bom import BomLine, aggregate_bom
from bom_manager.db import Inventory, PartDatabase
from bom_manager.parsers import LineItem


def make_line(pack_size=1, on_hand=0, quantity=0):
    return BomLine(
        key="k", footprint="McMaster", designation="94669A199",
        description="screw", customer_part="", quantity=quantity,
        type="fastener", pack_size=pack_size, on_hand=on_hand,
        vendor_hint="mcmaster",
    )


class TestPackMath:
    def test_need_6_pack_25_orders_1_pack_leftover_19(self):
        line = make_line(pack_size=25)
        assert line.packs_needed(6) == 1
        assert line.pieces_ordered(6) == 25
        assert line.leftover(6) == 19

    def test_stock_covers_need_orders_zero(self):
        line = make_line(pack_size=25, on_hand=19)
        assert line.packs_needed(6) == 0
        assert line.leftover(6) == 13

    def test_partial_stock_rounds_up_remaining(self):
        line = make_line(pack_size=25, on_hand=3)
        assert line.packs_needed(28) == 1
        assert line.leftover(28) == 0

    def test_no_pack_passes_need_through(self):
        line = make_line(pack_size=1, on_hand=0)
        assert line.packs_needed(7) == 7
        assert line.leftover(7) == 0

    def test_no_pack_with_stock(self):
        line = make_line(pack_size=1, on_hand=4)
        assert line.packs_needed(10) == 6
        assert line.leftover(10) == 0


def _item(designation, footprint="McMaster", qty=1, hint="mcmaster", category="mechanical"):
    return LineItem(
        chassis="Chassis1", source="MechanicalBOM", category=category,
        footprint=footprint, designation=designation, quantity=qty, vendor_hint=hint,
    )


class TestAggregate:
    def test_merges_duplicates(self, tmp_path):
        db = PartDatabase(tmp_path / "db.json")
        lines = aggregate_bom(iter([_item("94669A199", qty=2), _item("94669A199", qty=3)]), db)
        assert len(lines) == 1
        assert lines[0].quantity == 5

    def test_dno_excluded(self, tmp_path):
        db = PartDatabase(tmp_path / "db.json")
        db.exclude("McMaster", "94669A199")
        lines = aggregate_bom(iter([_item("94669A199"), _item("91292A134")]), db)
        assert [l.designation for l in lines] == ["91292A134"]

    def test_pack_size_and_stock_from_stores(self, tmp_path):
        db = PartDatabase(tmp_path / "db.json")
        db.add("McMaster", "94669A199", {
            "description": "screw", "type": "fastener", "pack_size": 25,
            "mouser_part": "", "digikey_part": "", "mcmaster_part": "94669A199",
        })
        inv = Inventory(tmp_path / "inv.json")
        inv.set("McMaster", "94669A199", 19)
        inv.save()
        lines = aggregate_bom(iter([_item("94669A199", qty=6)]), db, inventory=inv)
        assert lines[0].pack_size == 25
        assert lines[0].on_hand == 19
        assert lines[0].packs_needed(6) == 0

    def test_spares_cheap_rounds_mcmaster(self, tmp_path):
        db = PartDatabase(tmp_path / "db.json")
        lines = aggregate_bom(iter([_item("94669A199", qty=3)]), db, spares="cheap")
        assert lines[0].quantity == 10

    def test_vendor_list_pn_fallback(self):
        line = make_line()
        assert line.primary_vendor() == "mcmaster"
        assert line.vendor_part_number("mcmaster") == "94669A199"
