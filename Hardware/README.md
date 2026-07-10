# Hardware Variants

The platform centers on a common control board that can be paired with multiple power-stage classes. The following chassis variants define the current and planned inverter implementations:

| Variant | Application | DC Bus Voltage | Phase Current | Status |
|---|---|---|---|---|
| Chassis Size 1 | Low-medium power drives | 100&ndash;450 V | 50&ndash;200 A | Planned |
| Chassis Size 2 | Medium power drives | Up to 800 V (capacitor-dependent) | 600 A | Implemented, under test |
| Chassis Size 3 | High power drives | Up to 1200 V (capacitor-dependent) | 1400 A | In development |

Semiconductor ratings are selected with margin for the target DC bus: the 800 V class uses 1200 V rated parts, and the 1200 V class uses 1700 V rated parts. All chassis are designed with isolated logic and power.
