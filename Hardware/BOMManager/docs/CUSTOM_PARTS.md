# Custom Parts (McMaster-Carr and SendCutSend)

## McMaster-Carr Parts

McMaster-Carr has an approved-customer Product Information API (see
`API_KEYS.md`). If you configure the certificate-based credentials, the tool
uses the official API; otherwise it falls back to cached or manually set
prices.

### The mechanical list

`Hardware/<Chassis>/Mechanical/MechanicalBOM.txt` is owned by the tool — add
and change rows from the shell instead of editing by hand:

```text
bom> mech                                   # list with prices
bom> mech add McMaster 94669A199 12 M3x10 SHCS
bom> mech set 94669A199 20
bom> mech rm 94669A199
```

The file stays a plain CSV (`Qty,Vendor,PN,Description`), sorted and
deduplicated on every change. Any vendor string works (`McMaster`, `Digikey`,
`Mitsubishi`, ...). Set prices with `price <pn> <usd>`, and if the part comes
in a box, `pack <pn> 25 <box price>` — ordering then counts packs, not pieces.

## SendCutSend Fabricated Parts

Bus bars, plates, brackets, and other laser/waterjet-cut parts live one folder
per part under `Hardware/<Chassis>/Mechanical/Fab/<PartName>/`. Scaffold a new
one from the shell:

```text
bom> new fab DC Link Bus Bar
```

which creates:

```text
Hardware/<Chassis>/Mechanical/Fab/HW-C2-DCLBB-A/
├── info.txt        # specs and price (template)
└── README.md       # what goes here
```

Drop in `<folder>.step` (required) and `info.png` (optional, embedded in the
price report).

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
| PartName | Human-readable name. Words like "bus bar", "plate", "bracket" drive category detection. |
| Material | e.g., `Copper`, `5052 aluminum` |
| Thickness_mm | Sheet thickness in mm |
| Qty | Quantity per chassis |
| Dimensions_in or Dimensions_mm | Part dimensions |
| Finish | e.g., `as cut`, `Bending`, `tinned`, `anodized` |
| UnitPrice | USD per part |
| Process | e.g., `Sheet Cutting` |
| Notes | Design notes, tolerances, etc. |
| Rev | Managed by the tool (`rev bump`) — do not edit by hand. |

Each part gets an internal part number such as `HW-C2-BB-DCLBB-A`
(see `PART_NUMBERING.md`).

### Importing from a SendCutSend cart

After building a cart on SendCutSend, copy the page text and paste it:

```text
bom> import sendcutsend            # paste, then Ctrl+D
bom> import sendcutsend cart.txt   # or from a file
bom> import sendcutsend cart.txt --dry-run
```

The importer matches cart filenames to existing folders regardless of token
order (`C2_HW_BSP_A.step` matches `HW-C2-BSP-A`), creates missing folders, and
writes/updates each `info.txt` — including the quoted price.

### Revisions

`rev bump DCLBB --note "..."` bumps the revision and writes `Rev=` into
`info.txt`. Folders and STEP files are never renamed, so vendor re-uploads and
CAD links keep working.
