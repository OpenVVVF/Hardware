"""Scaffold new parts: fabricated parts, wiring harnesses, PCB boards.

Each `new ...` command creates the folder structure the rest of the tool
expects, drops a README explaining what goes where, and pre-registers
part-number descriptors so `generate` never has to prompt mid-run.
"""

import re
from pathlib import Path
from typing import Optional

from .context import Context
from .descriptor_registry import DescriptorRegistry

FAB_INFO_TEMPLATE = """\
PartName={part_name}
Material=
Thickness_mm=
Qty=1
Dimensions_in=
Finish=
UnitPrice=
Process=
Notes=
"""

FAB_README = """\
# {part_name}

Fabricated part (SendCutSend or similar shop).

## What goes here

- `{folder}.step` — CAD model uploaded to the vendor (required)
- `info.png` — render or photo, embedded in the price report (optional)
- `info.txt` — specs and price; edit by hand, or paste a SendCutSend cart
  into `import sendcutsend` and it will fill this in for you

## info.txt fields

| Field | Meaning |
|-------|---------|
| PartName | Human-readable name ("DC Link Bus Bar") — drives category detection |
| Material | e.g. Copper, 5052 aluminum |
| Thickness_mm | Sheet thickness |
| Qty | Quantity per chassis |
| Dimensions_in / Dimensions_mm | Part dimensions |
| Finish | e.g. as cut, Bending, tinned |
| UnitPrice | USD per part |
| Process | e.g. Sheet Cutting |
| Notes | Design notes, tolerances |

## Revisions

The folder name stays put forever. Revisions are tracked by the tool:
`rev bump {desc}` updates the registry and writes `Rev=` into info.txt.
Do NOT rename the folder for a new revision — CAD and vendor links point here.
"""

HARNESS_README = """\
# {name} wiring harness

A wiring harness documented as a KiCad schematic (a PCB layout is fine to
keep around, but nothing needs to be routed).

## How to document the harness

1. Create a KiCad schematic in this folder (e.g. `{name}.kicad_sch`).
2. Place a symbol for every physical part:
   - connectors and crimp terminals — use their real manufacturer part number
     as the symbol value where possible (e.g. `0039303040`);
   - wire — any symbol with a value starting with `WIRE`, e.g.
     `WIRE 10AWG RED`; put the length/gauge in the value or a field.
3. Export the BOM **from the schematic editor** (Eeschema) — no layout needed:
   - KiCad 7/8: **Tools → Generate BOM...**
   - Format: CSV, grouped by value;
   - include at least these fields: `Quantity`, `Footprint`, `Designation`
     (value), `Designator`;
   - save it as `Fab/{name}.csv` (the same layout as the boards).
4. Run `generate`. The harness components are consolidated into the BOM like
   any other part, and the harness itself appears as assembly line
   `{ipn}`.

Tip: naming the folder with the full part number (e.g. `HW-C2-WH-GD-A`) makes
`generate` adopt that number directly — no descriptor prompt, ever.

## Revisions

`rev bump {desc}` bumps the harness revision — no folder renames.
"""

BOARD_README = """\
# {name}

PCB design folder.

## What goes here

- Your KiCad project (`{name}.kicad_pro`, schematic, layout).
- `Fab/` — fabrication outputs:
  - `{name}.csv` — BOM exported from KiCad
    (**File → Fabrication Outputs → BOM CSV**; the tool expects the columns
    `Id;Designator;Footprint;Quantity;Designation;Supplier and ref`);
  - gerbers/drill files for the fab house.

`generate` picks up `Fab/{name}.csv`, adds a `{ipn}` fabrication line, and
bundles the whole `Fab/` folder into `FabricationData/PCB_Fab_Zips/{name}.zip`
for upload.

Set the quoted fab price with: `fab pcb-price {name} <usd>` (or paste a JLCPCB
cart into `import jlcpcb`).
"""


def _slug(text: str) -> str:
    return re.sub(r"[^A-Z0-9]+", "-", text.strip().upper()).strip("-")


def _write(path: Path, content: str) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(content, encoding="utf-8")


def new_fab_part(ctx: Context, chassis: str, part_name: str) -> Optional[Path]:
    """Create Mechanical/Fab/HW-C#-<DESC>-A/ with info.txt and README."""
    desc = _slug(part_name)
    if not desc:
        print("Part name cannot be empty.")
        return None
    abbr = DescriptorRegistry.abbreviate_chassis(chassis)
    folder = ctx.hardware_root / chassis / "Mechanical" / "Fab" / f"HW-{abbr}-{desc}-A"
    if folder.exists():
        print(f"Already exists: {folder}")
        return None
    folder.mkdir(parents=True)
    _write(folder / "info.txt", FAB_INFO_TEMPLATE.format(part_name=part_name.strip()))
    _write(folder / "README.md", FAB_README.format(part_name=part_name.strip(), folder=folder.name, desc=desc))
    print(f"Created {folder}")
    print("Next: drop the STEP file in, fill info.txt (or 'import sendcutsend' from a cart).")
    return folder


def new_harness(ctx: Context, chassis: str, name: str) -> Optional[Path]:
    """Create Wiring/<Name>/Fab/ with a README and pre-registered descriptor."""
    desc = _slug(name)
    if not desc:
        print("Harness name cannot be empty.")
        return None
    folder = ctx.hardware_root / chassis / "Wiring" / name.strip()
    if folder.exists():
        print(f"Already exists: {folder}")
        return None
    (folder / "Fab").mkdir(parents=True)
    abbr = DescriptorRegistry.abbreviate_chassis(chassis)
    ipn = f"HW-{abbr}-WH-{desc}-A"
    _write(folder / "README.md", HARNESS_README.format(name=name.strip(), desc=desc, ipn=ipn))
    # Pre-register so generate never prompts for this harness's descriptor.
    ctx.descriptor_registry.set(chassis, "wiring", name.strip(), desc)
    ctx.descriptor_registry.save()
    print(f"Created {folder / 'Fab'}")
    print(f"Harness assembly line will be {ipn}. See the README for the KiCad export steps.")
    return folder


def new_board(ctx: Context, chassis: str, name: str, descriptor: str = "") -> Optional[Path]:
    """Create Boards/<Name>/Fab/ with a README and pre-registered PCB descriptor."""
    clean = name.strip()
    if not clean or re.search(r"[^A-Za-z0-9_\-]", clean):
        print("Board name must be letters/digits/dash/underscore, e.g. 'PowerBoard'.")
        return None
    board_dir = ctx.hardware_root / chassis / "Boards" / clean
    if board_dir.exists():
        print(f"Already exists: {board_dir}")
        return None

    if not descriptor:
        default = DescriptorRegistry.default_for_board(clean)
        try:
            answer = input(f"Descriptor for {clean} [{default}]: ").strip()
        except EOFError:
            answer = ""
        descriptor = answer or default
    descriptor = DescriptorRegistry._clean_descriptor(descriptor)

    (board_dir / "Fab").mkdir(parents=True)
    abbr = DescriptorRegistry.abbreviate_chassis(chassis)
    ipn = f"HW-{abbr}-PCB-{descriptor}-A"
    _write(board_dir / "README.md", BOARD_README.format(name=clean, ipn=ipn))
    ctx.descriptor_registry.set(chassis, "pcb", clean, descriptor)
    ctx.descriptor_registry.save()
    print(f"Created {board_dir / 'Fab'}")
    print(f"Board fab line will be {ipn}. Export the KiCad BOM CSV as Fab/{clean}.csv — see README.")
    return board_dir
