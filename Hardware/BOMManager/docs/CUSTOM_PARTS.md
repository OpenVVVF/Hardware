# Custom Parts (McMaster-Carr and SendCutSend)

## McMaster-Carr Parts

McMaster-Carr has an approved-customer Product Information API (see `API_KEYS.md`). If you configure the certificate-based credentials, the tool uses the official API; otherwise it falls back to scraping the public product page or using a manually maintained CSV.

### Mechanical BOM format

Place a file at:

```
Hardware/Chassis2/Mechanical/MechanicalBOM.txt
```

With contents:

```csv
Qty,Vendor,PN
12,McMaster,94669A199
6,McMaster,94669A190
```

You can also include a `Description` column. The tool will look up each PN in the part database; if no database entry exists it will create one with the scraped/manual price.

### Adding McMaster parts manually

Use `manage_parts.py` and set `McMaster P/N`. You can also add a manual price if you do not want to rely on scraping.

## SendCutSend Fabricated Parts

For bus bars, plates, brackets, and other laser/waterjet cut parts, create one folder per part under:

```
Hardware/Chassis2/Mechanical/Fab/<PartName>/
```

Example:

```
Hardware/Chassis2/Mechanical/Fab/HW-C2-DCLBB-A/
├── HW-C2-DCLBB-A.step
├── info.png
└── info.txt
```

### `info.txt` fields

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

| Field | Description |
|-------|-------------|
| PartName | Human-readable name. Use words like "bus bar", "plate", or "bracket" so the tool picks the right category. |
| Material | e.g., `Copper`, `5052 aluminum` |
| Thickness_mm | Sheet thickness in mm |
| Qty | Quantity per chassis |
| Dimensions_in or Dimensions_mm | Part dimensions |
| Finish | e.g., `as cut`, `Bending`, `tinned`, `anodized` |
| UnitPrice | USD per part |
| Process | e.g., `Sheet Cutting` |
| Notes | Design notes, tolerances, etc. |

### Attachments

- **STEP file**: name it `<folder_name>.step` (or any `.step/.stp`). The path is listed in the report.
- **info.png**: a render or photo of the part. If present, it is embedded in the price report's SendCutSend details section.

Each folder-based part automatically gets an internal part number such as `HW-BB-0001-A`. See `PART_NUMBERING.md` for category conventions.

## Importing from a SendCutSend cart

After getting a quote on SendCutSend, copy the cart text and run:

```bash
python3 import_sendcutsend_cart.py
# paste the cart text, then press Ctrl+D
```

Or save the cart text to a file:

```bash
python3 import_sendcutsend_cart.py cart.txt --chassis Chassis2
```

This parses the quote and writes/updates the `info.txt` in the matching folder under `Hardware/Chassis2/Mechanical/Fab/`. Run a dry-run first to preview:

```bash
python3 import_sendcutsend_cart.py cart.txt --dry-run
```

The importer extracts:

- Part name from the `.step` filename
- Material and thickness (e.g., `Copper (.187")`)
- Dimensions (e.g., `12.357 x 0.551 in`)
- Services such as `Bending`
- Unit price and total price (used to compute quantity)

The importer matches cart filenames to existing folders regardless of token order, so `C2_HW_BSP_A.step` will match a folder named `HW-C2-BSP-A`. If the folder does not exist yet, the importer creates it for you.

### Tip: use descriptive PartName values

The tool auto-detects the part category from `PartName`. For example:

- `PartName=DC Link Bus Bar` → category `busbar` → `HW-BB-0001-A`
- `PartName=Capacitor Heat Spreader Plate` → category `plate` → `HW-PLT-0001-A`
- `PartName=Gate Driver Mounting Bracket` → category `bracket` → `HW-BRK-0001-A`

If the auto-detected category is wrong, edit `info.txt` and re-run `generate_bom.py`.
