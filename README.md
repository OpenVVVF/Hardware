![Size 2 Inverter CAD Rendering](Size2.png)
![Size 0 Inverter CAD Rendering](Size0.png)

This project aims to provide open source inverter and software that can be used to drive 3 phase brushless motors in a wide array of power ranges. It orininated as an attempt to replace the existing inverter in an electric motorcycle and has since increased in scope to cover more than just that.

Whilst designing these inverters, attempting to keep them modular was a key design goal as this allows common electronics, firmware, and design methodology to be shared across various power classes.

All of these designs are released under open source licenses.

### Brief Overview

Each inverter is classified into a different chassis size* which roughly correspond to their voltage and current capabilities as can be seen below.
| Class | Power | Current | Voltage |
|---------|-------------|---------------|---------------|
| **Class 0** | 300W - 5kW | Up to 100A | 12V - 72V nominal |
| **Class 1** | 5kW - 40kW | 50A - 150A | 24V - 144V nominal |
| **Class 2** | 50kW - 300kW | 150A - 800A | 48V - 300V nominal |

<sub>*These are not final and are here just to give you an rough idea of the power ranges and voltages we are aiming for. These are very much subject to change!</sub>

All classes share a common firmware, control architecture, and communication protocol. All can be configured via our interface tool, Real Time Examiner (RTE), or by directly flashing the RP2040 (Pi Pico) with new firmware. More information about the software and specific inverters can be found within their directories.

### Project Status

As of 2/16/2026, the hardware design for two prototypes, a C2 and a C0, as well as a dev board has been completed with the latter having been assembled. It is being used as a testing platform for RTE and the underlying firmware which is in heavy development. Most basic features and functionality has been implemented. FOC is coming Soon<sup>TM</sup>!

### Getting Started

#### For Users

The BOM and all other necessary files for each variant are provided in their respective directories. 

#### For Contributors

Contributions to the project are very welcome and highly encouraged! This project uses KiCad for schematic and PCB design and FreeCAD for the mechanical. It is highly recommended you do as well! If you wish to make modifications existing designs, all source files are again provided in their respective directories.

When designing and contributing new designs, please attempt to maintain consistency with the existing design conventions and documentation standards. All new designs should include schematics, pcb layouts, BOMs and ideally some documentation/additional notes (and documentation is better than none!). If you have any questions feel free to reach out!