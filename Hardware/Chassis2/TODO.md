# Chassis 2 TODOs

- [ ] Evaluate Belleville washers for vibration resistance on motorcycle-mounted inverter fasteners.
  - Context: Loctite 242 was added to the mechanical BOM for thread locking, but a high-vibration motorcycle environment may need Belleville washers or a stronger threadlocker (e.g., Loctite 243/272) on critical fasteners.
  - Decide before releasing the mechanical BOM / assembly docs.

- [x] Fix and finalize internal part numbers for the DC link module holders.
  - Current placeholders: `HW-C2-DCLMH-200-PRINTED-B` and `HW-C2-DCLMH-450-PRINTED-A`.
  - `HW-C2-DCLMH-200-PRINTED-B` is registered (`part_numbers.json`); the 450V
    placeholder folder now exists under `Mechanical/Fab/` and is wired into the
    `450v` build variant in `variants.yaml` (variant-only folder — stays out of
    the 200v BOM; its IPN is assigned on the next `generate` run).
  - Done with the C2-DCDC-A release: `python3 bom.py generate` ran and the IPNs are registered.

- [x] Design actual DC link module holder geometry in FreeCAD.
  - 200V variant uses 35.5 mm tall caps.
  - 450V variant uses shorter caps (~5 mm shorter than 200V).
  - Done for C2-DCDC-A: STEP/STL/holes.json/renders exported from the model into
    `Mechanical/Fab/HW-C2-DCLMH-450-PRINTED-A/` and
    `Mechanical/Fab/HW-C2-DCLMH-450-SPCR-PRINTED-A/`; `info.txt` filled in
    (Thickness_mm=31.50 for the 450V holder).
  - Standoff (94669A196 -> 94669A193) and cap-spacer swap rules are live in the
    `450v` build variant in `variants.yaml` (also carried into the `dcdc` variant).
  - Thermal calcs will vary with aluminum heat-spreader plate thickness per voltage class.

- [ ] DC/DC converter module (C2-DCDC-A) follow-ups.
  - Released as the `dcdc` build variant (`variants.yaml`); orderable outputs in
    `FabricationData/Builds/dcdc/`; full assembly model `Mechanical/DC-DC-Module.FCStd`
    (+ exported `DC-DC-Module.step`).
  - Prices still missing for the Hammond 195C100 chokes, Boyd 416201U00000G
    coldplates, TDK B25645A1328K003 film cap, and the new McMaster frame
    fasteners — set with `bom> price <pn> <usd>` or import a cart.
