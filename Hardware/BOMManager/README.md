# BOM Manager

Your fab-time companion for the inverter build. It aggregates the Bill of
Materials from KiCad PCB exports, wiring-harness schematics, mechanical
hardware, and custom fabricated parts — then helps you price everything,
order the right quantities (including pack sizes and what is already on your
shelf), and keep fabrication packages ready to send out.

## The interactive shell

```bash
cd Hardware/BOMManager
python3 bom.py
```

```
bom> status                     # where you stand: lines, prices, attention items, fab readiness
bom> mech                       # the MechanicalBOM.txt hardware list (the tool owns this file)
bom> mech add McMaster 94669A199 12 M3x10 SHCS
bom> price 94669A199 3.10       # set a price (per piece, or per pack if packed)
bom> pack 94669A199 25          # sold in boxes of 25 -> ordering does the math
bom> stock 94669A199 19         # 19 left over on your shelf (stays local, never committed)
bom> add                        # walk through BOM lines that still need vendor PNs/prices
bom> new fab Phase Bus Bar      # scaffold a fabricated-part folder (+README)
bom> new harness GateDriverHarness
bom> new board PowerBoard
bom> rev bump DCLBB --note "widen holes"
bom> fab                        # fabrication package readiness: PCBs, fab parts, harnesses
bom> import mouser              # paste a cart/order, prices land in cache AND the committed DB
bom> import digikey ~/Downloads/digikey_bom.csv.xlsx
bom> release                    # order files + full PDF report (schematics, layers, pricing)
```

Every command also works one-shot for scripting: `python3 bom.py generate --qty 5`.
Type `help` inside the shell for the full list. **See `docs/GUIDE.md` for the
complete usage guide.**

The old scripts (`generate_bom.py`, `manage_parts.py`, `import_*.py`) still run
— they are thin shims that delegate to the shell.

## What `generate` and `release` produce

Per chassis, under `Hardware/<Chassis>/FabricationData/`:

```text
├── BOMs/
│   ├── Consolidated_BOM.csv     # everything, with Order Qty / Pack Size / Leftover columns
│   ├── mouser_bom.csv           # upload to the vendor's BOM tool
│   ├── digikey_bom.csv
│   ├── mcmaster_bom.csv
│   ├── sendcutsend_bom.csv
│   ├── pcb_bom.csv
│   └── assembly_bom.csv         # in-house assemblies (wiring harnesses)
├── McMaster_Order_Paste.txt     # paste straight into the McMaster cart
├── PCB_Fab_Zips/<Board>.zip     # per-board fab bundles
├── Assembly/<Board>.html        # interactive assembly view per board (iBOM; via release)
├── Pricing_Report.md            # costs, pack rounding, fabrication package checklist
└── Release_Report.pdf           # full release doc (HARA-style; via release): cover, pricing,
                                 # STEP previews, per-board 3D renders + schematic + layers
```

## How ordering math works

- A part with `pack_size = 25` and 6 needed → order **1 pack**, 19 left over.
- `stock` counts are subtracted first: 6 needed, 19 on hand → order 0.
- Order quantities (packs) are what land in vendor CSVs and the McMaster paste
  file; lines fully covered by stock are skipped there but still shown in the
  Consolidated BOM and the report's **Pack Rounding & Stock** section.
- Prices are per **order unit**: per piece normally, per pack when packed.

## Where data lives

Committed (project data, diff-friendly):

| File | Contents |
|------|----------|
| `bom_manager/data/part_database.json` | vendor PNs, pack sizes, manual prices, notes |
| `bom_manager/data/part_numbers.json` | internal PN registry with revision history |
| `bom_manager/data/part_descriptors.json` | short names used in part numbers |
| `Hardware/<Chassis>/Mechanical/MechanicalBOM.txt` | purchased hardware list (edit via `mech` commands) |
| `Hardware/<Chassis>/Mechanical/Fab/<Part>/` | fabricated parts: STEP + `info.txt` + image |
| `Hardware/<Chassis>/Wiring/<Name>/` | wiring harnesses: KiCad schematic + BOM CSV export |

Local only (gitignored): `config.yaml` (API keys), `price_cache.json`,
`inventory.json` (your on-hand stock — your leftover screws are not project data).

## Part numbers and revisions

Every BOM line gets an internal PN like `HW-C2-BB-DCLBB-A`
(chassis, category, descriptor, revision). Registry keys are **identity-based**,
so editing a description or friendly part name never renumbers anything, and
folder names never need to change for a revision:

```text
bom> rev list
bom> rev bump DCLBB --note "widen mounting holes"
```

For fabricated parts this writes `Rev=B` into the part's `info.txt` — the
folder and STEP file stay put, so CAD and vendor links keep working.

## First-time setup

```bash
pip install -r requirements.txt        # runtime deps
pip install -r requirements-dev.txt    # + pytest, for the test suite
python3 -m pytest tests/               # sanity check
cp config.yaml.example config.yaml     # optional: vendor API keys (docs/API_KEYS.md)
```

API keys are optional — the whole workflow runs on manual prices and pasted
cart/order imports.

## Docs

- `docs/GUIDE.md` — **complete usage guide (start here)**
- `docs/WORKFLOW.md` — end-to-end fab-time walkthrough
- `docs/CUSTOM_PARTS.md` — fabricated parts and SendCutSend
- `docs/PART_NUMBERING.md` — internal PN conventions and revisions
- `docs/API_KEYS.md` — optional vendor API setup
- `docs/SPARES_AND_REPORTING.md` — spares policies and report format
