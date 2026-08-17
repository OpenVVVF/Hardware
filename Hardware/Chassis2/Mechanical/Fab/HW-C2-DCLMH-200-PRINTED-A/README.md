# DC Link Module Holder - 200V Class

3D-printed part.

## What goes here

- `HW-C2-DCLMH-200-PRINTED-A.step` — CAD model exported from FreeCAD (required)
- `info.png` — render or photo, embedded in the price report (optional)
- `info.txt` — specs and price

## info.txt fields

| Field | Meaning |
|-------|---------|
| PartName | Human-readable name — drives category detection |
| Material | e.g. PETG, PLA, ABS |
| Thickness_mm | Sheet thickness / part height |
| Qty | Quantity per chassis |
| Dimensions_in / Dimensions_mm | Part dimensions |
| Finish | e.g. as printed, vapor smoothed |
| UnitPrice | USD per part |
| Process | e.g. 3D Printing |
| Notes | Design notes, tolerances |

## Revisions

The folder name stays put forever. Revisions are tracked by the tool:
`rev bump DCLMH-200-PRINTED` updates the registry and writes `Rev=` into info.txt.
Do NOT rename the folder for a new revision — CAD links point here.
