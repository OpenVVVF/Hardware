"""Shared fixtures: a synthetic Hardware tree and a Context pointed at tmp paths."""

import sys
from pathlib import Path

import pytest

sys.path.insert(0, str(Path(__file__).resolve().parent.parent))

from bom_manager.config import Config
from bom_manager.context import Context
from bom_manager.db import Inventory, PartDatabase
from bom_manager.descriptor_registry import DescriptorRegistry
from bom_manager.part_numbers import PartNumberRegistry
from bom_manager.pricing import PriceCache


@pytest.fixture
def ctx(tmp_path):
    data = tmp_path / "data"
    data.mkdir()
    hw = tmp_path / "Hardware"
    hw.mkdir()
    return Context(
        root=tmp_path,
        hardware_root=hw,
        config=Config(tmp_path / "config.yaml"),
        db=PartDatabase(data / "part_database.json"),
        cache=PriceCache(data / "price_cache.json"),
        inventory=Inventory(data / "inventory.json"),
        pn_registry=PartNumberRegistry(data / "part_numbers.json"),
        descriptor_registry=DescriptorRegistry(data / "part_descriptors.json"),
        mouser=None,
        digikey=None,
        octopart=None,
        mcmaster=None,
    )


def make_chassis(hw_root: Path, name: str = "Chassis1") -> Path:
    """Create a minimal chassis tree: one board, mech list, one fab part, one harness."""
    chassis = hw_root / name

    fab_dir = chassis / "Boards" / "MainBoard" / "Fab"
    fab_dir.mkdir(parents=True)
    (fab_dir / "MainBoard.csv").write_text(
        "Id;Designator;Footprint;Quantity;Designation;Supplier and ref\n"
        '1;"R1,R2";R_1210_3225Metric;2;10k;\n'
        '2;"C1";C_1210_3225Metric;1;100nF 100V;\n',
        encoding="utf-8",
    )

    mech_dir = chassis / "Mechanical"
    mech_dir.mkdir(parents=True)
    (mech_dir / "MechanicalBOM.txt").write_text(
        "Qty,Vendor,PN\n"
        "12,McMaster,94669A199\n"
        "3,Digikey,3M156058-ND\n",
        encoding="utf-8",
    )

    part = mech_dir / "Fab" / "HW-C1-PLATE-A"
    part.mkdir(parents=True)
    (part / "HW-C1-PLATE-A.step").write_text("ISO-10303-21;", encoding="utf-8")
    (part / "info.txt").write_text(
        "PartName=HW-C1-PLATE-A\nMaterial=Copper\nThickness_mm=4.75\nQty=2\n"
        "UnitPrice=50.71\nProcess=Sheet Cutting\n",
        encoding="utf-8",
    )

    harness = chassis / "Harnesses" / "GDHarness"
    harness.mkdir(parents=True)
    (harness / "GDHarness.csv").write_text(
        "Id;Designator;Footprint;Quantity;Designation;Supplier and ref\n"
        '1;"J1,J2";Connector_Molex_2x2;2;0039303040;\n'
        '3;"W1";Wire_10AWG;1;WIRE 10AWG RED;\n',
        encoding="utf-8",
    )

    (chassis / "variants.yaml").write_text(
        "default: base\n"
        "variants:\n"
        "  base:\n"
        "    description: Base build\n"
        "    rules: []\n"
        "  alt:\n"
        "    description: Alt build\n"
        "    rules:\n"
        "      - exclude: {designation: 100nF 100V}\n"
        "      - add: {designation: 220nF 100V, footprint: C_1210_3225Metric, category: board,\n"
        "              source: MainBoard, quantity: 1, description: Alt bank cap}\n"
        "      - exclude: {source: HW-C1-PLATE-A}\n",
        encoding="utf-8",
    )
    return chassis
