# Chassis 2 TODOs

- [ ] Evaluate Belleville washers for vibration resistance on motorcycle-mounted inverter fasteners.
  - Context: Loctite 242 was added to the mechanical BOM for thread locking, but a high-vibration motorcycle environment may need Belleville washers or a stronger threadlocker (e.g., Loctite 243/272) on critical fasteners.
  - Decide before releasing the mechanical BOM / assembly docs.

- [ ] Fix and finalize internal part numbers for the DC link module holders.
  - Current placeholders: `HW-C2-DCLMH-200-PRINTED-B` and `HW-C2-DCLMH-450-PRINTED-A`.
  - `HW-C2-DCLMH-200-PRINTED-B` is registered (`part_numbers.json`); the 450V
    placeholder folder now exists under `Mechanical/Fab/` and is wired into the
    `450v` build variant in `variants.yaml` (variant-only folder — stays out of
    the 200v BOM; its IPN is assigned on the next `generate` run).
  - Run `python3 bom.py generate` once BOMManager deps are installed to register them and confirm final IPNs.

- [ ] Design actual DC link module holder geometry in FreeCAD.
  - 200V variant uses 35.5 mm tall caps.
  - 450V variant uses shorter caps (~5 mm shorter than 200V).
  - `Mechanical/Fab/HW-C2-DCLMH-450-PRINTED-A/` is a placeholder: info.txt only,
    no STEP/CAD yet — geometry is designed (InverterMechanical.FCStd); export STEP/STL
    into the folder before release. Same for `HW-C2-DCLMH-450-SPCR-PRINTED-A/`.
  - Standoff (94669A196 -> 94669A193) and cap-spacer swap rules are live in the
    `450v` build variant in `variants.yaml`.
  - Update `Thickness_mm` and STEP files when real dimensions are known.
  - Thermal calcs will vary with aluminum heat-spreader plate thickness per voltage class.
