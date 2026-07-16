"""Internal part numbers: identity keys, migration, revisions, history."""

from bom_manager.part_numbers import PartNumberRegistry, line_identity


class FakeLine:
    def __init__(self, footprint, designation, hint=None, source="Chassis1/MainBoard"):
        self.footprint = footprint
        self.designation = designation
        self.vendor_hint = hint
        self.sources = [source]


def test_generate_creates_formatted_pn_with_history(tmp_path):
    reg = PartNumberRegistry(tmp_path / "pn.json")
    pn = reg.generate_pn("resistor", "10k 1210", "10K1210", chassis="C2", identity="r_1210|10k")
    assert pn == "HW-C2-RES-10K1210-A"
    entry = reg.lookup_by_pn(pn)
    assert entry["history"][0]["rev"] == "A"
    assert entry["history"][0]["note"] == "created"


def test_identity_stable_across_description_edits(tmp_path):
    """Editing a part's description must not renumber it."""
    reg = PartNumberRegistry(tmp_path / "pn.json")
    pn1 = reg.generate_pn("mechanical", "94669A199", "94669A199", chassis="C2", identity="mcmaster|94669a199")
    pn2 = reg.generate_pn("mechanical", "M3 socket head screw", "94669A199", chassis="C2", identity="mcmaster|94669a199")
    assert pn1 == pn2


def test_legacy_key_migrates_and_keeps_pn(tmp_path):
    reg = PartNumberRegistry(tmp_path / "pn.json")
    legacy_key = reg._make_key("busbar", "HW-C2-DCLBB-A", "C2")
    reg._data[legacy_key] = {
        "part_number": "HW-C2-BB-DCLBB-A", "category": "busbar",
        "category_prefix": "BB", "descriptor": "DCLBB", "revision": "A",
        "description": "HW-C2-DCLBB-A", "key": legacy_key, "chassis": "C2",
    }
    pn = reg.generate_pn("busbar", "HW-C2-DCLBB-A", "DCLBB", chassis="C2", identity="fab:hw-c2-dclbb-a")
    assert pn == "HW-C2-BB-DCLBB-A"
    assert reg.lookup(legacy_key) is None
    assert reg.lookup(reg._make_identity_key("busbar", "fab:hw-c2-dclbb-a", "C2")) is not None


def test_descriptor_scan_finds_renamed_part(tmp_path):
    """A friendly PartName change still resolves via chassis+category+descriptor."""
    reg = PartNumberRegistry(tmp_path / "pn.json")
    pn1 = reg.generate_pn("busbar", "HW-C2-DCLBB-A", "DCLBB", chassis="C2", identity="fab:hw-c2-dclbb-a")
    pn2 = reg.generate_pn("busbar", "DC Link Bus Bar", "DCLBB", chassis="C2", identity="fab:hw-c2-dclbb-a")
    assert pn1 == pn2


def test_bump_revision_records_history(tmp_path):
    reg = PartNumberRegistry(tmp_path / "pn.json")
    reg.generate_pn("plate", "HW-C2-CHSP-A", "CHSP", chassis="C2", identity="fab:hw-c2-chsp-a")
    key = reg._make_identity_key("plate", "fab:hw-c2-chsp-a", "C2")
    pn = reg.bump_revision(key, note="thicker")
    assert pn == "HW-C2-PLT-CHSP-B"
    entry = reg.lookup(key)
    assert entry["revision"] == "B"
    assert entry["history"][-1]["note"] == "thicker"
    pn = reg.bump_revision(key, new_rev="D")
    assert pn == "HW-C2-PLT-CHSP-D"


def test_find_by_pn_descriptor_description(tmp_path):
    reg = PartNumberRegistry(tmp_path / "pn.json")
    reg.generate_pn("busbar", "HW-C2-DCLBB-A", "DCLBB", chassis="C2", identity="fab:hw-c2-dclbb-a")
    assert reg.find("HW-C2-BB-DCLBB-A")
    assert reg.find("dclbb")
    assert reg.find("dc link".replace("dc link", "DCLBB-A"))


def test_line_identity_fab_uses_folder():
    line = FakeLine("", "DC Link Bus Bar", hint="sendcutsend", source="Chassis2/HW-C2-DCLBB-A")
    assert line_identity(line) == "fab:hw-c2-dclbb-a"


def test_line_identity_normal():
    line = FakeLine("R_1210_3225Metric", "10k")
    assert line_identity(line) == "r_1210_3225metric|10k"
