# Chassis 2 TODOs

- [ ] Evaluate Belleville washers for vibration resistance on motorcycle-mounted inverter fasteners.
  - Context: Loctite 242 was added to the mechanical BOM for thread locking, but a high-vibration motorcycle environment may need Belleville washers or a stronger threadlocker (e.g., Loctite 243/272) on critical fasteners.
  - Decide before releasing the mechanical BOM / assembly docs.

- [ ] Fix and finalize internal part numbers for the DC link module holders.
  - Current placeholders: `HW-C2-DCLMH-200-PRINTED-A` and `HW-C2-DCLMH-450-PRINTED-A`.
  - Run `python3 bom.py generate` once BOMManager deps are installed to register them and confirm final IPNs.

- [ ] Make DC link module variants for 250V / 350V / 400V / 450V voltage classes.
  - Need clarification: are these separate capacitor banks / holders, or just documentation/BOM variants under the existing 250-450V class?
  - Scaffold folders / STEP placeholders / BOM entries once the physical differences are defined.
