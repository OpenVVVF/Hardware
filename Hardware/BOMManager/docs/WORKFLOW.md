# BOM Manager Workflow

The whole flow runs inside the interactive shell:

```bash
cd Hardware/BOMManager
python3 bom.py
```

(Everything below also works one-shot, e.g. `python3 bom.py generate --qty 5`.)

## 1. Feed the tool your design data

**PCB BOMs** — for each board, in KiCad: **File → Fabrication Outputs → BOM CSV**,
saved as `Hardware/<Chassis>/Boards/<Board>/Fab/<Board>.csv`. The same folder is
zipped for the fab house, so put gerbers/drill files there too.

**New parts** — scaffold instead of hand-creating folders:

```text
bom> new fab Phase Bus Bar        # Mechanical/Fab/HW-C2-PBB-A/ + info.txt + README
bom> new board PowerBoard         # Boards/PowerBoard/Fab/ + README, descriptor registered
bom> new harness GateDriverHarness # Wiring/GateDriverHarness/Fab/ + README
```

**Wiring harnesses** — document as a KiCad *schematic only* (no layout). Place
symbols for connectors/crimps/wire (wire values start with `WIRE`), then export
the BOM CSV from Eeschema (**Tools → Generate BOM**) into the harness folder.
The scaffolded README spells out the exact export settings. Components land in
the BOM like any other part; the harness itself appears as a `HW-C2-WH-...`
assembly line.

**Purchased hardware** — the tool owns `MechanicalBOM.txt`; do not hand-edit:

```text
bom> mech add McMaster 94669A199 12 M3x10 socket head screw
bom> mech set 94669A199 20
bom> mech rm 94669A199
```

## 2. Price things

```text
bom> status           # what still needs attention
bom> add              # wizard: walks BOM lines missing vendor PNs/prices
bom> price 94669A199 3.10
bom> pack 94669A199 25 24.40     # box of 25 at $24.40/box
bom> fab pcb-price ControlBoard 25.24
```

Prices attach to the committed part database. `pack` means: order N packs of
25, not N pieces.

## 3. Generate and order

```text
bom> generate
```

Outputs land in `Hardware/<Chassis>/FabricationData/` (see README). Ordering:

- **Mouser / DigiKey**: upload the respective `*_bom.csv` to the vendor BOM tool.
- **McMaster**: paste `McMaster_Order_Paste.txt` into the cart's
  "Paste part numbers and quantities" box. Quantities are already in packs, and
  parts covered by your shelf stock are left out.
- **SendCutSend**: specs are in the report's fab section; upload the STEP files
  from `Mechanical/Fab/<Part>/`.
- **PCBs**: upload `PCB_Fab_Zips/<Board>.zip` to your fab.

## 4. Import real prices after ordering

Paste the cart/order confirmation (then Ctrl+D):

```text
bom> import mouser
bom> import mcmaster
bom> import sendcutsend       # fills/updates info.txt in Fab folders
bom> import jlcpcb            # per-board PCB fab prices
```

Imported prices go to the local cache **and** persist into the committed part
database, so cost history survives a fresh clone.

## 5. Receive parts, record leftovers

```text
bom> stock 94669A199 19       # 19 screws left over
```

Stock lives in `inventory.json`, which is gitignored — your shelf is not
project data. Future `generate` runs subtract it: covered parts order 0 packs
and show up in the report's **Pack Rounding & Stock** section.

## 6. Revisions

```text
bom> rev list
bom> rev bump DCLBB --note "widen mounting holes"
```

Bumps update the registry (with history) and write `Rev=` into a fabricated
part's `info.txt`. Folders and STEP files are never renamed — CAD and vendor
links keep working, and internal PNs never change when you edit descriptions.

## 7. Track cost over time

Commit `FabricationData/Pricing_Report.md`, `part_database.json`, and
`part_numbers.json`. Rerun `generate` as the design evolves and diff the report.

For scripted/CI runs use `--no-prompt` so the tool errors instead of blocking
when something needs input.
