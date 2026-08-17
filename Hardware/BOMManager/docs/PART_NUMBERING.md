# Internal Part Numbering Conventions

Every BOM line gets an internal part number (IPN) in the format:

```text
HW-<CHASSIS>-<CATEGORY>-<DESCRIPTOR>-<REV>
```

Examples:

- `HW-C2-PCB-CTRL-A` — Chassis2 control board PCB, revision A
- `HW-C2-PCB-GD-A` — Chassis2 gate driver PCB, revision A
- `HW-C2-WH-GDVS-A` — Chassis2 gate-driver-to-??? wiring harness, revision A
- `HW-C2-BB-DCLBB-A` — Chassis2 DC link bus bar, revision A
- `HW-C2-PLT-CHSP-A` — Chassis2 capacitor heat spreader plate, revision A
- `HW-C2-RES-10K1210-A` — Chassis2 10k 1210 resistor
- `HW-C2-IC-STM32H723ZGTX-A` — Chassis2 STM32 microcontroller

The registry lives in `bom_manager/data/part_numbers.json` and is generated automatically. Registry keys are **identity-based** (`chassis|category|footprint|designation`, or `chassis|category|fab:<folder>` for fabricated parts), so editing a description or a friendly `PartName` never renumbers a part. (Registries written by older versions used description-based keys; entries are migrated automatically on first contact, keeping their part numbers.) The tool also keeps a descriptor registry at `bom_manager/data/part_descriptors.json` so you only have to name a part once.

## Category reference

Use these categories when prompted, or let the tool auto-detect them.

| Category | Prefix | Use for |
|----------|--------|---------|
| `pcb` | `HW-PCB` | Printed circuit boards fabricated by a PCB house |
| `busbar` | `HW-BB` | Bus bars, current-carrying bars, tie bars |
| `plate` | `HW-PLT` | Heat spreaders, mounting plates, cover plates, shields |
| `bracket` | `HW-BRK` | Mounting brackets, standoffs, supports |
| `wiring` | `HW-WH` | Wiring harnesses, wire assemblies, leads |
| `cable` | `HW-CBL` | Discrete cables, pre-made cable assemblies |
| `3dprint` | `HW-3DP` | 3D printed parts |
| `connector` | `HW-CONN` | Connectors, headers, sockets, terminals |
| `fastener` | `HW-FST` | Screws, bolts, nuts, washers, standoffs |
| `mechanical` | `HW-MECH` | Other McMaster / mechanical hardware |
| `electrical` | `HW-ELEC` | Misc electrical parts not covered above |
| `resistor` | `HW-RES` | SMD / through-hole resistors |
| `capacitor` | `HW-CAP` | SMD / through-hole capacitors |
| `ic` | `HW-IC` | ICs, crystals, microcontrollers, isolators |
| `assembly` | `HW-ASSY` | Sub-assemblies made of multiple parts |
| `other` | `HW-HW` | Anything that does not fit above |

## How descriptors are chosen

The tool tries to pick a good descriptor automatically and only asks you when it needs a human decision.

| Part class | How descriptor is chosen |
|------------|--------------------------|
| **PCB fab** | Pre-registered by `new board` (or prompted on first run). Default is an abbreviation of the board folder name (`ControlBoard` → `CTRL`). |
| **Wiring harness** | Pre-registered by `new harness`. Default is the harness folder/document name. |
| **Wire / cable in a harness** | Auto from the value (e.g. `WIRE 10AWG RED` → `WIRE-10AWG-RED`), no prompt. |
| **Bus bars / plates / brackets / 3D prints** | Auto from the fabricated-part folder name (`DCLBB`, `CHSP`, `BSP`). |
| **Mechanical / fasteners** | Auto from the McMaster part number or slugified description. |
| **Resistors / capacitors / ICs / connectors** | Auto from value, footprint, or manufacturer part number. |

You can edit `bom_manager/data/part_descriptors.json` directly at any time to rename descriptors. After editing, rerun `python3 bom.py generate`.

### Example descriptor registry

```json
{
  "Chassis2|pcb|controlboard": "CTRL",
  "Chassis2|pcb|gatedriver": "GD",
  "Chassis2|pcb|dcbuscapacitorboard": "DCBUSCAP",
  "Chassis2|pcb|dcbusfilter": "DCFLT",
  "Chassis2|pcb|ioboard": "IO"
}
```

## Concrete examples

| Real-world part | Category | Descriptor | Resulting IPN |
|-----------------|----------|------------|---------------|
| Control board PCB | `pcb` | `CTRL` | `HW-C2-PCB-CTRL-A` |
| Gate driver PCB | `pcb` | `GD` | `HW-C2-PCB-GD-A` |
| DC bus capacitor bulk board PCB | `pcb` | `DCBUSCAP` | `HW-C2-PCB-DCBUSCAP-A` |
| DC bus filter PCB | `pcb` | `DCFLT` | `HW-C2-PCB-DCFLT-A` |
| IO board PCB | `pcb` | `IO` | `HW-C2-PCB-IO-A` |
| DC link positive bus bar | `busbar` | `DCLBB` | `HW-C2-BB-DCLBB-A` |
| Phase bus bar | `busbar` | `PBB` | `HW-C2-BB-PBB-A` |
| Capacitor heat spreader plate | `plate` | `CHSP` | `HW-C2-PLT-CHSP-A` |
| Base / structural plate | `plate` | `BSP` | `HW-C2-PLT-BSP-A` |
| IGBT module mounting bracket | `bracket` | `IGBTBRK` | `HW-C2-BRK-IGBTBRK-A` |
| Gate driver to control board wiring harness | `wiring` | `GDCTRL` | `HW-C2-WH-GDCTRL-A` |
| Current sensor cable | `cable` | `CURSENS` | `HW-C2-CBL-CURSENS-A` |
| 3D printed connector cover | `3dprint` | `CONCOV` | `HW-C2-3DP-CONCOV-A` |
| M3x10 socket head screw | `fastener` | `M3X10SHS` | `HW-C2-FST-M3X10SHS-A` |
| 1210 10k resistor | `resistor` | `10K1210` | `HW-C2-RES-10K1210-A` |
| 1210 100nF capacitor | `capacitor` | `100NF100V1210` | `HW-C2-CAP-100NF100V1210-A` |
| STM32 microcontroller | `ic` | `STM32H723ZGTX` | `HW-C2-IC-STM32H723ZGTX-A` |

## Naming fabricated mechanical parts

For SendCutSend / fabricated parts, use the folder name as the part name. The recommended convention is:

```text
HW-<CHASSIS>-<FUNCTION>-<REV>
```

Examples:

| Folder name | Part meaning | Category | IPN |
|-------------|--------------|----------|-----|
| `HW-C2-DCLBB-A` | Chassis2 DC Link Bus Bar | `busbar` | `HW-C2-BB-DCLBB-A` |
| `HW-C2-PBB-A` | Chassis2 Phase Bus Bar | `busbar` | `HW-C2-BB-PBB-A` |
| `HW-C2-CHSP-A` | Chassis2 Capacitor Heat Spreader Plate | `plate` | `HW-C2-PLT-CHSP-A` |
| `HW-C2-BSP-A` | Chassis2 Base / Back Structural Plate | `plate` | `HW-C2-PLT-BSP-A` |

Abbreviations the tool auto-detects for category:

- `DCLBB`, `DCLB`, `PBB`, `BB` → `busbar`
- `CHSP`, `HSP`, `HS`, `BSP` → `plate`
- `BRK`, `BRACKET` → `bracket`

## Multiple chassis / sizes

Part numbers are chassis-specific by default. A physically different part in another chassis gets a different chassis prefix, even if the descriptor is the same.

```text
Chassis2 DC link bus bar    → HW-C2-BB-DCLBB-A
Chassis3 DC link bus bar    → HW-C3-BB-DCLBB-A  (different size = different PN)
```

Use the descriptor to identify the function and the chassis prefix to identify the size/revision of the overall chassis.

## Revisions

Bump a revision when the part changes enough that you need to distinguish old stock from new stock:

- `A` — first revision
- `B` — changed hole pattern, material, thickness, connector pinout, etc.
- `C`, `D`, ... — subsequent changes
- The tool starts at `A` automatically.

For off-the-shelf commodity parts (resistors, capacitors, ICs), the revision letter is kept for format consistency but is usually left at `A` because you are not revising the part itself.

To bump a revision:

```text
bom> rev list
bom> rev bump DCLBB --note "widen mounting holes"
```

The query is fuzzy — a full IPN, a descriptor, or description text. The bump is
recorded in the entry's `history` (rev, date, note) inside `part_numbers.json`.

Revisions are **decoupled from file and folder names**: bumping writes `Rev=`
into a fabricated part's `info.txt` and never renames its folder or STEP file,
so CAD documents and vendor uploads keep pointing at the same place.

## Wiring harnesses

A harness documented as a KiCad schematic gets an assembly line
like `HW-C2-WH-GD-A` once you drop the schematic's BOM CSV export into
`Hardware/<Chassis>/Wiring/<Name>/` (or its `Fab/` subfolder, same layout as
boards). Create the folder with `new harness` —
it pre-registers the descriptor and its README has the exact Eeschema export
settings. Naming the folder with the full part number (`HW-C2-WH-GD-A`) makes
`generate` adopt that number with no prompt at all. The harness's connectors,
crimps, and wire appear as normal BOM lines with their own IPNs.

## Auto-detection

The tool guesses the category from the footprint, description, and source/folder name:

- `R_1210...` footprints → `resistor`
- `C_1210...` footprints → `capacitor`
- Footprints containing `IC`, `SOIC`, `QFP`, etc. → `ic`
- Description containing "bus bar" or folder name containing `DCLBB`/`PBB` → `busbar`
- Description containing "plate" or folder name containing `CHSP`/`HSP`/`BSP` → `plate`
- Description containing "bracket" or folder name containing `BRK` → `bracket`
- KiCad board source → `pcb`
- McMaster mechanical parts → `mechanical`

If it guesses wrong, you can override the category with `add` (the wizard's Type prompt) in the shell.

## Non-interactive / CI usage

If you run the tool in a script and it encounters a part that needs a descriptor, it will prompt by default. Use `--no-prompt` to make it fail with a clear error instead:

```bash
python3 bom.py generate --no-prompt
```

Add the missing descriptors to `bom_manager/data/part_descriptors.json` and rerun.
