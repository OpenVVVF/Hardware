# BOM Manager

Aggregates Bill of Materials from KiCad PCB exports, mechanical hardware, and custom fabricated parts across one or more chassis revisions. Generates per-vendor shopping lists, PCB fab zip bundles, and an auto-updated price report.

## What this tool does

1. Finds all BOM sources under `Hardware/Chassis*/` automatically.
2. Applies your part database (vendor part numbers, prices, exclusions).
3. Assigns every line an internal part number with revision tracking.
4. Adds one `HW-PCB-XXXX-A` line per board and zips its `Fab/` folder for vendor upload.
5. Looks up live prices from Mouser, DigiKey, Octopart, and McMaster-Carr when API keys are configured.
6. Writes vendor-ready CSVs, a McMaster cart paste file, and a Markdown price report per chassis.

## First-time setup

```bash
cd /home/tliao/Desktop/InverterGen5/Hardware/BOMManager

# 1. Install dependencies
pip install -r requirements.txt

# 2. Copy the example config and add your API keys (optional — the tool works without them)
cp config.yaml.example config.yaml
# Edit config.yaml with your keys
```

See `docs/API_KEYS.md` for how to obtain keys. `config.yaml` is gitignored so keys are never committed.

## Run the tool

```bash
python3 generate_bom.py
```

Outputs appear per chassis, for example:

```text
Hardware/Chassis2/FabricationData/
├── BOMs/
│   ├── Consolidated_BOM.csv
│   ├── mcmaster_bom.csv
│   ├── mouser_bom.csv
│   ├── digikey_bom.csv
│   ├── pcb_bom.csv
│   ├── sendcutsend_bom.csv
│   └── unknown_bom.csv
├── McMaster_Order_Paste.txt
├── PCB_Fab_Zips/
│   ├── ControlBoard.zip
│   ├── DCBusCapacitorBoard.zip
│   ├── DCBusFilter.zip
│   ├── GateDriver.zip
│   └── IOBoard.zip
└── Pricing_Report.md
```

## Common commands

```bash
# Default: all chassis, 1 unit, no spares
python3 generate_bom.py

# Price 5 units, round cheap parts to standard pack sizes
python3 generate_bom.py --qty 5 --spares cheap

# Only Chassis2, only specific boards
python3 generate_bom.py --chassis Chassis2 --board ControlBoard,GateDriver

# Refresh cached prices from vendors
python3 generate_bom.py --refresh-prices

# Interactively add missing generic resistors/capacitors
python3 generate_bom.py --suggest

# Edit the part database, SendCutSend parts, or internal part numbers
python3 manage_parts.py

# Import prices back from vendor carts/orders
python3 import_mouser_cart.py
python3 import_mouser_cart.py /path/to/mouser_bom.xlsx
python3 import_mcmaster_order.py
python3 import_sendcutsend_cart.py
python3 import_jlcpcb_order.py
```

## Where to put your project data

The tool reads from standard locations under `Hardware/Chassis*/`:

| What | Where |
|------|-------|
| PCB BOMs | `Chassis*/Boards/<BoardName>/Fab/<BoardName>.csv` |
| PCB fab zip output | `Chassis*/FabricationData/PCB_Fab_Zips/<BoardName>.zip` |
| Mechanical fasteners / McMaster parts | `Chassis*/Mechanical/MechanicalBOM.txt` |
| Custom fabricated parts (SendCutSend) | `Chassis*/Mechanical/Fab/<PartName>/info.txt` |
| Final reports and vendor CSVs | `Chassis*/FabricationData/` |

See `docs/WORKFLOW.md` for a complete walkthrough, `docs/CUSTOM_PARTS.md` for SendCutSend folder format, and `docs/PART_NUMBERING.md` for how to categorize parts like bus bars, plates, PCBs, and wiring harnesses.

## Files in this directory

```text
BOMManager/
├── generate_bom.py              # main CLI
├── manage_parts.py              # interactive editor
├── import_sendcutsend_cart.py   # import SendCutSend cart text to info.txt
├── import_mouser_cart.py        # import Mouser cart prices to cache
├── import_mcmaster_order.py     # import McMaster order prices to cache
├── import_jlcpcb_order.py       # import JLCPCB PCB fab prices to cache
├── config.yaml.example          # copy to config.yaml and fill in keys
├── requirements.txt
├── README.md
├── docs/
│   ├── API_KEYS.md              # how to get API keys
│   ├── CUSTOM_PARTS.md          # McMaster + SendCutSend parts
│   ├── PART_NUMBERING.md        # internal PN conventions
│   ├── SPARES_AND_REPORTING.md  # spares policies and report format
│   └── WORKFLOW.md              # end-to-end walkthrough
├── bom_manager/
│   └── data/
│       ├── part_database.json   # your substitutions / vendor mappings (commit this)
│       ├── part_numbers.json    # internal PN registry (commit this)
│       ├── price_cache.json     # cached prices (gitignored)
│       └── sendcutsend_manifest.csv
└── output/                      # legacy single-directory output (kept for --output-dir)
```

## Migration Note

The original `MouserBOM.py`, `SubstitutionManager.py`, and `bom_substitutions.json` from `Hardware/Chassis2/Boards/` have been moved to `legacy/`. The substitution data was migrated into `bom_manager/data/part_database.json`.

Output has moved from `BOMManager/output/` to per-chassis `Hardware/Chassis*/FabricationData/`. You can still use `--output-dir <path>` to override.
