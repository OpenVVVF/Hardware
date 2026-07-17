# BOM Manager — Usage Guide

The fab-time tool for this project. It collects the BOM from your KiCad
boards, wiring harnesses, mechanical hardware, and fabricated parts; helps you
price and order everything (with pack-size math and your on-hand stock); and
builds the release package (order files + full PDF report).

## Setup

```bash
cd Hardware/BOMManager
pip install -r requirements.txt
pip install -r requirements-dev.txt   # pytest, for the test suite
python3 -m pytest tests/              # sanity check
```

`config.yaml` (vendor API keys) is optional — everything works on manual
prices and pasted cart/order imports.

## The shell

```bash
python3 bom.py            # interactive
python3 bom.py status     # one-shot (any command works this way)
```

Everything below is a shell command. The shell remembers your chassis
(`chassis <name>` to switch) and never dies on a bad command — fix and retry.

## Daily loop: parts, prices, packs, stock

```text
status                    # dashboard: lines, priced coverage, attention items
parts                     # every BOM line with vendor PN + price status
parts --unpriced          # just what's missing prices
add                       # wizard: walk lines that need entries
price 94669A199 3.10      # set a price (per piece, or per pack if packed)
pack 94669A199 25         # sold in boxes of 25 -> ordering counts packs
stock 94669A199 19        # leftovers on your shelf (local, gitignored)
exclude <query>           # mark a part DNO (do not order)
forget <query>            # delete a part's database entry
```

Rules of thumb:

- **Qty = pieces the design needs**, never packs. `pack` carries the pack
  size; ordering does `ceil((need − stock) / pack_size)`.
- Prices are per **order unit** (per piece normally, per pack when packed).
- The query for price/pack/stock is a vendor PN, internal PN, or description
  text — a picker appears when it's ambiguous.

## MechanicalBOM.txt is owned by the tool

Don't hand-edit it. It holds purchased chassis hardware (any vendor string):

```text
mech                            # list with prices
mech add McMaster 94669A199 12 M3x10 SHCS
mech set 94669A199 6
mech rm 94669A199
```

Board-level parts and harness parts do **not** belong here — those come from
KiCad.

## Boards

Export each board's BOM **from the schematic editor** (Eeschema → Tools →
Generate BOM) as `Boards/<Board>/<Board>.csv` (beside the project). Both the
classic `Id;Designator;Footprint;Quantity;Designation` format and the KiCad
10 `Reference,Qty,Value,DNP,...` export work; DNP/excluded rows are skipped.
`Fab/` holds gerbers/drill — those get zipped for the fab house.

Parts covered elsewhere (e.g. a connector that ships with a harness) should
be **Exclude from BOM** in the schematic so they aren't double-ordered.

```text
fab pcb-price ControlBoard 25.24    # set a board's fab price
import jlcpcb                        # or paste a JLCPCB cart to set them all
```

## Wiring harnesses

One folder per harness under `Wiring/<Name>/`: a KiCad schematic (layout not
needed) plus the schematic BOM export as `<Name>.csv` in the same folder.

```text
new harness GDVSHarness     # scaffolds folder + README + descriptor
```

- Components (connectors, crimps, wire) consolidate into the BOM like any
  other part; the harness itself appears as a `HW-C2-WH-...` assembly line.
- **Descriptor** comes from `new harness`, from the folder name when it's
  IPN-shaped (`HW-C2-WH-GD-A` or `HW-C2-WH-GD` — adopted with no prompt), or
  from a one-time prompt otherwise.
- **Building several per chassis?** The schematic documents ONE harness; add
  `info.txt` with `Qty=4` next to the CSV. The assembly line and all
  components multiply.

Current harnesses as an example: GDVS ×1, TSIGBT ×2, CS ×4, TSBCM ×1.

## Fabricated parts (SendCutSend & friends)

```text
new fab DC Link Bus Bar     # Mechanical/Fab/HW-C2-DCLBB-A/ + info.txt + README
```

Drop the STEP in, fill `info.txt` (or paste a cart into `import sendcutsend`
and it fills material/thickness/price/qty for you). `fab` shows readiness
(STEP ✓, info.txt ✓, price ✓, image ✓) and the same checklist lands in the
report.

## Ordering

```text
release
```

runs the full generate and prints an upload checklist:

| Vendor | File | Where |
|--------|------|-------|
| Mouser | `BOMs/mouser_bom.csv` | Mouser BOM tool |
| DigiKey | `BOMs/digikey_bom.csv` | DigiKey BOM tool |
| McMaster | `McMaster_Order_Paste.txt` | cart "paste part numbers" box (quantities are packs; stock-covered lines omitted) |
| SendCutSend | `Mechanical/Fab/*/` STEPs | specs in the report |
| PCB | `PCB_Fab_Zips/*.zip` | your PCB vendor |

## Importing real prices back

After carts/orders exist, import them — prices land in the local cache **and**
the committed part database (so cost history survives a fresh clone):

```text
import mouser ~/Downloads/mouser_bom.xlsx    # BOM-tool xlsx or pasted text
import digikey ~/Downloads/digikey.csv.xlsx  # BOM-tool export
import mcmaster                              # paste order text, Ctrl+D
import jlcpcb                                 # paste cart text (per-board fab prices)
import sendcutsend                            # paste cart text (fills info.txt)
```

The Mouser importer also resolves parts you uploaded by manufacturer PN and
writes the resolved Mouser PNs back into the database, so the next upload is
fully populated. Unmatched lines are reported, not silently dropped.

## Revisions

```text
rev list
rev bump DCLBB --note "widen mounting holes"
```

- Internal PNs (`HW-C2-BB-DCLBB-A`) are identity-keyed — editing descriptions
  or friendly names never renumbers anything.
- Bumping writes `Rev=` into a fab part's `info.txt` and records history
  (rev, date, note) in `part_numbers.json`. **Folders and files are never
  renamed** — CAD and vendor links keep working.
- For PCBs, the rev on the silkscreen is yours to maintain: bump in the tool
  and update the silk text in the same sitting, so boards say what they are.
- When you bump, re-export vendor-facing artifacts (STEP, PDFs) with the new
  rev in the filename.

## Regen: change the design, run one command

```text
regen
```

re-exports everything from KiCad and rebuilds the BOM:

- each board: BOM CSV (`sch export bom`), gerbers + drill into `Fab/`,
  board STEP model, and a DRC report at `FabricationData/DRC/<Board>.txt`
  (**errors only** — warnings are suppressed for now; counts printed per board)
- each harness: BOM CSV from its schematic
- then a normal `generate` runs

The loop is: edit schematics/layout in KiCad → `regen` → order/check the
outputs. `release` afterwards for the PDF + assembly HTML.

## The release PDF

`release` also builds `FabricationData/Release_Report.pdf` (styled after the
project docs, same format as `Docs/HARA.pdf`):

- Cover with totals and the 1/2/3/5/10-unit scaling table
- Contents page, order summary (per-vendor tables, pack-aware)
- Fabrication package (readiness incl. harness qty/rev + a page per fab part
  with a **fresh 3D preview rendered from its STEP file**, colored by material)
- Per board: divider (name, IPN, **3D board renders** top + bottom) → full
  schematic → PCB layer plots (all copper + silkscreen) → **DRC summary page**
  (errors only, from the regen reports)
- Per harness: divider → schematic
- **Design documents**: every document in `Docs/` (HARA, TARA, SWAD, Manual,
  analyses) is a part with its own IPN (`HW-C2-DOC-...`), compiled into the
  package — the PDF you ship with the device.

**Document standard: all project docs are Markdown** (`Docs/*.md`) compiled by
one template (docgen: cover page, auto-TOC with page numbers, footers, green
HARA style). Edit the `.md`, run `release`, and both `Docs/*.pdf` and the
package pick it up. No HTML, no LaTeX (use Unicode: ×, Δ, ρ, µ, °).

Rendering: board 3D renders and layer plots come from KiCad CLI (flatpak via
`flatpak run --command=kicad-cli org.kicad.KiCad`); fab-part previews are
rendered from STEP with cadquery + matplotlib (no GL needed); documents are
compiled with weasyprint. `bom.py` automatically re-executes itself with the
bundled `.venv` python when one exists, so dependencies always resolve.

## Assembly HTML (iBOM)

`release` also writes `FabricationData/Assembly/<Board>.html` per board — an
interactive assembly view (click a BOM line to highlight its parts on the
board, front/back views). Open in any browser; great for hand-stuffing.

## Cost by assembly & QC forms

The release PDF's **Cost by Assembly** section breaks the BOM down per
sub-assembly (each board, each harness, each fab part, chassis hardware) with
parts counts and per-assembly cost.

`FabricationData/QC_Forms.pdf` — one printable page per sub-assembly: header
(IPN, rev, qty per chassis), a checkbox parts list with per-assembly
quantities, an inspection checklist (per assembly kind), and a
sign-off block (assembled / inspected / signatures) for the assembler.

## Data files

Committed: `part_database.json` (PNs, pack sizes, prices), `part_numbers.json`
(IPNs + revision history), `part_descriptors.json`, `MechanicalBOM.txt`,
fab part folders, harness folders.

Gitignored (local): `config.yaml`, `price_cache.json`, `inventory.json`
(your shelf stock).

## Command cheat sheet

```text
status | parts [--missing|--unpriced|--vendor X|--type Y] [query]
add | price <q> <usd> | pack <q> <size> [price] | stock [q] [n]
mech [add <vendor> <pn> <qty> [desc] | set <pn> <qty> | rm <pn>]
new fab|harness|board [name]
rev list | rev bump <q> [--rev X] [--note "..."]
fab | fab pcb-price <board> <usd>
import mouser|digikey|mcmaster|sendcutsend|jlcpcb [file]
generate [--qty N --spares none|cheap|all --chassis X --no-prompt]
release
exclude <q> | include <q> | forget <q> | chassis [name] | quit
```
