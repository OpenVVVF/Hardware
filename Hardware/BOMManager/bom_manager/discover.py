"""Discover BOM source files under Hardware/."""

from dataclasses import dataclass
from pathlib import Path
from typing import Iterator, List, Optional


@dataclass(frozen=True)
class BomSource:
    chassis: str
    category: str  # 'board', 'mechanical', 'custom', 'sendcutsend_folder'
    board: str     # board name for boards, or source filename stem for others
    path: Path
    vendor_hint: Optional[str] = None


def discover_boms(
    hardware_root: Path,
    chassis_filter: Optional[List[str]] = None,
    board_filter: Optional[List[str]] = None,
) -> Iterator[BomSource]:
    """Yield BOM sources found under hardware_root/Chassis*/..."""
    for chassis_dir in sorted(hardware_root.iterdir()):
        if not chassis_dir.is_dir():
            continue
        name = chassis_dir.name
        if not name.lower().startswith("chassis"):
            continue
        chassis = name
        if chassis_filter and chassis not in chassis_filter:
            continue

        boards_dir = chassis_dir / "Boards"
        if boards_dir.is_dir():
            for board_dir in sorted(boards_dir.iterdir()):
                if not board_dir.is_dir():
                    continue
                board = board_dir.name
                if board_filter and board not in board_filter:
                    continue
                # Preferred: <Board>.csv beside the KiCad project (BOMs live in
                # the project dir). The name must match the folder so stray
                # export dirs (e.g. Boards/BOMs/) are not picked up.
                root_csv = board_dir / f"{board}.csv"
                if root_csv.is_file():
                    yield BomSource(chassis, "board", board, root_csv)
                    continue
                # Legacy: KiCad Fab output folder.
                fab_dir = board_dir / "Fab"
                if fab_dir.is_dir():
                    for csv_file in sorted(fab_dir.glob("*.csv")):
                        if csv_file.name.lower().startswith("bom") or csv_file.stem.lower() == board.lower():
                            yield BomSource(chassis, "board", board, csv_file)

        mech_dir = chassis_dir / "Mechanical"
        if mech_dir.is_dir():
            for path in sorted(mech_dir.iterdir()):
                if path.is_file() and path.name.lower().startswith("mechanicalbom") and path.suffix.lower() in (".txt", ".csv"):
                    yield BomSource(chassis, "mechanical", path.stem, path, vendor_hint="mcmaster")

        custom_dir = chassis_dir / "CustomParts"
        if custom_dir.is_dir():
            for path in sorted(custom_dir.glob("*.csv")):
                yield BomSource(chassis, "custom", path.stem, path, vendor_hint="sendcutsend")

        # Wiring harnesses documented as KiCad schematics: one BOM CSV export
        # per harness under Wiring/<Name>/ or Harnesses/<Name>/, either in the
        # folder root or a Fab/ subfolder (same layout as boards).
        for harness_root in ("Wiring", "Harnesses"):
            harness_dir = chassis_dir / harness_root
            if not harness_dir.is_dir():
                continue
            for part_dir in sorted(harness_dir.iterdir()):
                if not part_dir.is_dir() or part_dir.name.startswith("."):
                    continue
                candidates = sorted(part_dir.glob("*.csv"))
                fab_sub = part_dir / "Fab"
                if fab_sub.is_dir():
                    candidates += sorted(fab_sub.glob("*.csv"))
                for csv_file in candidates:
                    yield BomSource(chassis, "harness", part_dir.name, csv_file)

        # Discover SendCutSend/fabricated part folders under Mechanical/Fab/ or Mechanical/SendCutSendParts/
        for scs_name in ("Fab", "SendCutSendParts"):
            scs_dir = mech_dir / scs_name
            if scs_dir.is_dir():
                for part_dir in sorted(scs_dir.iterdir()):
                    if not part_dir.is_dir() or part_dir.name.startswith("."):
                        continue
                    if (part_dir / "info.txt").exists() or list(part_dir.glob("*.step")) + list(part_dir.glob("*.stp")):
                        yield BomSource(chassis, "sendcutsend_folder", part_dir.name, part_dir, vendor_hint="sendcutsend")
