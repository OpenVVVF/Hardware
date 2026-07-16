# BOM Manager Workflow

This guide walks through the entire process from exporting a KiCad BOM to generating the final price report.

## 1. Export PCB BOMs from KiCad

For each board, generate the Fab BOM CSV:

1. Open the board in KiCad.
2. **File → Fabrication Outputs → BOM CSV**.
3. Save as `Hardware/Chassis2/Boards/<BoardName>/Fab/<BoardName>.csv`.

The tool expects these columns: `Id;Designator;Footprint;Quantity;Designation;Supplier and ref`.

The same folder will be zipped into `Hardware/Chassis2/FabricationData/PCB_Fab_Zips/<BoardName>.zip` for vendor upload.

## 2. Add McMaster fasteners

Create or edit:

```text
Hardware/Chassis2/Mechanical/MechanicalBOM.txt
```

Format:

```csv
Qty,Vendor,PN
12,McMaster,94669A199
6,McMaster,94669A190
```

Save. The tool will detect these and try to look up prices.

## 3. Add custom fabricated parts (SendCutSend)

For each fabricated part, create a folder:

```text
Hardware/Chassis2/Mechanical/Fab/DCPlusBusBar/
├── DCPlusBusBar.step
├── info.png
└── info.txt
```

`info.txt`:

```ini
PartName=DC Link Bus Bar
Material=Copper
Thickness_mm=4.75
Qty=2
Dimensions_in=12.357x0.551 in
Finish=Bending
UnitPrice=50.71
Process=Sheet Cutting
Notes=Tin plate ends after forming
```

The `info.png` is optional but recommended — it will appear in the price report.

See `CUSTOM_PARTS.md` for the full field reference and naming conventions.

### Importing from a SendCutSend cart

After building a cart on SendCutSend, copy the page text and run:

```bash
python3 import_sendcutsend_cart.py
# paste the cart text, then press Ctrl+D
```

This parses the quote and writes/updates the `info.txt` in the matching `Fab/<PartName>/` folder. Add `--dry-run` to preview first.

## 4. Run the generator

```bash
cd /home/tliao/Desktop/InverterGen5/Hardware/BOMManager
python3 generate_bom.py
```

The first time you run the tool (or any time a new PCB or wiring harness appears), it will prompt you for short descriptors:

```text
Need a descriptor for Chassis2 pcb: ControlBoard
  (PCB board descriptor)
  Default: CTRL
  Descriptor (Enter to accept default):
```

These are saved to `bom_manager/data/part_descriptors.json`, so you will not be prompted again unless you add a new board or harness.

You will see output like:

```text
Discovered 10 BOM sources.

=== Chassis2 ===
Consolidated to 83 unique BOM lines.
  Wrote BOMs/mcmaster_bom.csv
  Wrote BOMs/mouser_bom.csv
  Wrote BOMs/pcb_bom.csv
  Wrote BOMs/sendcutsend_bom.csv
  Wrote BOMs/unknown_bom.csv
  Wrote McMaster_Order_Paste.txt
  Wrote BOMs/Consolidated_BOM.csv
  Wrote Pricing_Report.md
  Wrote PCB_Fab_Zips/ControlBoard.zip
  ...
```

Outputs are written per chassis:

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

If you later add Chassis3, the tool will create `Hardware/Chassis3/FabricationData/` automatically.

For scripted/CI runs, use `--no-prompt` so the tool errors instead of blocking for input when a descriptor is missing.

## 5. Handle missing parts

Open `Hardware/Chassis2/FabricationData/Pricing_Report.md` and scroll to **Unknown / Missing Prices**. These are parts the tool could not price.

For generic 1210 resistors and capacitors:

```bash
python3 generate_bom.py --suggest
```

This will propose in-stock, low-cost options and save your choice to the database.

For everything else, add vendor part numbers manually:

```bash
python3 manage_parts.py
# choose option 1 (Add/Edit part database entry)
```

You can also add a manual price if you do not want to rely on API lookups.

## 6. Order parts

- **Mouser**: upload `Hardware/Chassis2/FabricationData/BOMs/mouser_bom.csv` to the Mouser BOM Tool. After you get cart/order pricing, paste the order text back into:
  ```bash
  python3 import_mouser_cart.py
  ```
- **DigiKey**: upload `Hardware/Chassis2/FabricationData/BOMs/digikey_bom.csv` to the DigiKey BOM Tool.
- **McMaster**: copy `Hardware/Chassis2/FabricationData/McMaster_Order_Paste.txt` and paste into the McMaster cart's "Paste part numbers and quantities" box. After ordering, paste the order confirmation text back into:
  ```bash
  python3 import_mcmaster_order.py
  ```
- **SendCutSend**: use the specs in the report's SendCutSend section and upload the STEP files from `Hardware/Chassis2/Mechanical/Fab/<PartName>/`.
- **PCB fabrication**: upload the zip files in `PCB_Fab_Zips/` to your PCB vendor. Add the quoted PCB price to the part database (set a manual price on the `HW-PCB-XXXX-A` lines) so the report includes it.

These import scripts update `bom_manager/data/price_cache.json` so the report reflects real prices instead of API guesses.

## 7. Track project cost over time

Commit the following to git:

- `Hardware/Chassis2/FabricationData/Pricing_Report.md`
- `bom_manager/data/part_database.json`
- `bom_manager/data/part_numbers.json`

Each time you rerun `generate_bom.py`, the report updates, so you can diff cost changes as the design evolves.

`config.yaml` and `bom_manager/data/price_cache.json` are gitignored and should not be committed.
