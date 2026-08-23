# DC Link Module Holder - 200V Class

3D-printed part.

## What goes here

- `HW-C2-DCLMH-200-PRINTED-B.step` — CAD model exported from FreeCAD (required)
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

The revision is part of the part number, in both the folder name and the
FreeCAD body/group label. To bump: rename the label in the FCStd model
(e.g. `...-PRINTED-A` → `...-PRINTED-B`), rename this folder and the STEP
inside it to match, then re-run the extraction (STEP/STL/holes/model_parts
are regenerated from the model). The release pipeline (HWRelease) keys on
the label suffix, so folder name, label, and STEP filename must agree.
