## Overview


![Size 2 Inverter CAD Rendering](Size2.png)

This project provides open source power electronics designs for motor drives spanning a wide power range, from small robotics drives to large traction inverters. The platform is designed with modularity at its core, allowing common electronics, firmware, and design methodology to be shared across all chassis sizes while optimizing mechanical and thermal design for each power class.

The project originated as a personal academic endeavor and has grown into a comprehensive inverter platform suitable for various applications including agricultural robots, electric motorcycles, industrial vehicles, marine propulsion, and heavy-duty traction systems. All designs are released under open source licenses to enable innovation, education, and collaboration within the power electronics community.

Each chassis size represents a complete design ecosystem including power stages, gate drivers, control boards, filtering, and mechanical integration. The unified model numbering system allows users to quickly identify the specifications of any inverter variant, while the shared electronic architecture simplifies firmware development, component sourcing, and community support.


---

## Platform Architecture

The inverter platform is organized into chassis sizes that correspond to power and current ratings. This tiered approach enables designers to select the appropriate platform for their application while benefiting from shared electronics and firmware across all sizes. Larger chassis sizes accommodate higher current ratings and support higher nominal battery voltages, but all sizes share common control architecture, communication protocols, and design philosophy.

| Chassis | Power Range | Current Range | Voltage Range | Primary Applications |
|---------|-------------|---------------|---------------|----------------------|
| **Size 0** | 300W - 5kW | Up to 100A | 12V - 72V nominal | Robotics |
| **Size 1** | 5kW - 40kW | 50A - 150A | 24V - 144V nominal | E-bikes |
| **Size 2** | 50kW - 300kW | 150A - 800A | 48V - 300V nominal | Electric Motorcycles |

---

## Model Numbering System

All inverters follow a consistent model numbering format that clearly communicates the key specifications at a glance. This system facilitates inventory management, documentation, and communication between developers and users.

### Format

**`C[SIZE][CLASS][VOLTAGE]N[CURRENT]-[FEATURES]`**

| Segment | Description | Options |
|---------|-------------|---------|
| `C` | Chassis prefix | Fixed (indicates chassis-based design) |
| `[SIZE]` | Chassis size | 0, 1, 2 |
| `[CLASS]` | Voltage class | LV (≤350V max) or HV (>350V max) |
| `[VOLTAGE]` | Nominal voltage | 48N, 106N, 132N, 165N, 230N, 265N, 300N |
| `[CURRENT]` | Peak current | 50A, 75A, 100A, 150A, 300A, 450A, 600A, 800A |
| `[FEATURES]` | Feature codes | A0/A4 (analog inputs), L12/L15/L24/L48 (logic supply), C (CANbus) |

### Voltage Class

The voltage class (LV or HV) distinguishes between low-voltage and high-voltage designs. This designation affects component selection, creepage and clearance requirements, and isolation ratings throughout the inverter.

| Class | Maximum Voltage | Typical Use |
|-------|-----------------|-------------|
| **LV** | ≤350V | Battery voltages up to approximately 300V nominal |
| **HV** | >350V | High-voltage battery systems, traction applications |

### Voltage Codes

| Code | Nominal Voltage | Maximum Voltage | Typical Battery |
|------|-----------------|-----------------|-----------------|
| 48N | 48V | ~60V | 12S Li-ion, lead-acid |
| 106N | 106V | ~120V | 72S Li-ion (84V nominal) |
| 132N | 132V | ~150V | 96S Li-ion (115V nominal) |
| 165N | 165V | ~200V | 120S Li-ion (144V nominal) |
| 230N | 230V | ~280V | 168S Li-ion (201V nominal) |
| 265N | 265V | ~320V | 192S Li-ion (230V nominal) |
| 300N | 300V | ~360V | 216S Li-ion (259V nominal) |

### Example Model Numbers

| Model Number | Interpretation |
|--------------|----------------|
| C0LV48N75A-A4L12C | Size 0, LV, 48V nominal, 75A peak, 4 analog inputs, 12V logic supply, CANbus |
| C1LV106N100A-A4L24C | Size 1, LV, 106V nominal, 100A peak, 4 analog inputs, 24V logic supply, CANbus |
| C2LV230N600A-A4L12C | Size 2, LV, 230V nominal, 600A peak, 4 analog inputs, 12V logic supply, CANbus |

---

## Chassis Size Details

### Size 0: Robotics Drive (300W - 5kW)

Size 0 represents the entry point of the platform, designed for applications requiring compact form factors and moderate power levels. These drives are well-suited for educational purposes, research platforms, and small-scale robotic systems. The designs emphasize ease of modification and low component costs to encourage experimentation and learning.

This size employs MOSFET-based power stages exclusively, selected for their low gate charge and fast switching characteristics at lower voltages. The gate driver and control architecture maintains compatibility with larger sizes, allowing firmware and control algorithms to be developed and tested on Size 0 before deployment on larger platforms.

| Parameter | Specification |
|-----------|---------------|
| Power Range | 300W - 5kW |
| Current Range | Up to 100A peak |
| Voltage Range | 12V - 72V nominal |
| Typical Applications | Small AGVs, educational robots, UAV ground support, laboratory research |
| Design Focus | Compact size, low cost, accessibility |

### Size 1: Small EV Drive (5kW - 40kW) (coming soon)

Size 1 bridges the gap between small robotics drives and full-sized electric vehicle inverters. These designs serve the growing market for light electric vehicles including e-bikes, small electric motorcycles, lawn and garden equipment, and small marine propulsion systems. The power density achievable at this size makes it suitable for applications where weight and space are critical considerations.

The designs support standard 48V, 72V, and 144V battery systems commonly used in small electric vehicles. All Size 1 designs share the same control architecture and communication protocols as larger chassis sizes, simplifying system integration for customers who may scale up to Size 2 in future product iterations.

This size uses IGBT modules exclusively, leveraging commercial power modules that integrate multiple IGBT chips in rugged packages rated for high current operation.

| Parameter | Specification |
|-----------|---------------|
| Power Range | 5kW - 40kW |
| Current Range | 50A - 150A peak |
| Voltage Range | 100V - 600V nominal |
| Typical Applications | E-bikes, small electric motorcycles, lawn equipment, small marine vessels, light AGVs |
| Design Focus | High power density, cost-effective, easy integration |

### Size 2: Standard EV Drive (50kW - 300kW)

Size 2 serves as the primary platform for most electric vehicle and industrial applications. The designs accommodate a wide range of battery voltages and current ratings, making them suitable for electric motorcycles, small passenger vehicles, forklifts, medium marine vessels, and industrial automation systems. This size represents the most comprehensive variant of the platform with full feature support across all voltage and current options.

The mechanical design supports both liquid and forced air cooling depending on the specific power rating and application requirements. All boards within Size 2 are designed to be interchangeable across variants, meaning the same control board can be used from 150A to 800A and from 48V to 300V nominal, with only the power stage, gate drivers, and bulk capacitor boards changing to accommodate different specifications.

This size uses IGBT modules exclusively, including high-current commercial modules capable of handling the full 800A peak current range without paralleling discrete devices.

| Parameter | Specification |
|-----------|---------------|
| Power Range | 50kW - 300kW |
| Current Range | 150A - 800A peak |
| Voltage Range | 48V - 300V nominal |
| Typical Applications | Electric motorcycles, small EVs, forklifts, medium marine vessels, industrial AGVs, material handling |
| Design Focus | Versatility, reliability, comprehensive feature set |

---

## Technical Overview

### Control and Modulation Architecture

All chassis sizes share a unified control architecture built around the RP2040 microcontroller (Pi Pico), enabling sophisticated motor control with extensive configurability.

**Switching Frequency:** The platform supports dynamic carrier frequency modulation from **300Hz to 8kHz**, allowing real-time optimization between switching losses and current ripple based on operating conditions. Lower frequencies (300Hz-1kHz) minimize switching losses during high-torque, low-speed operation, while higher frequencies (4kHz-8kHz) reduce current ripple and motor noise during high-speed cruising.

**Modulation Patterns:** The firmware implements **ARS-SV-PWM (Advanced Regular-Sampled Space Vector Pulse Width Modulation)** using the RP2040's PIO (Programmable I/O) blocks for deterministic, jitter-free timing. This architecture enables:
- Synchronous sampling of phase currents at the optimal point in the PWM cycle
- Seamless transitions between modulation strategies (sinusoidal, space vector, overmodulation)
- Precise dead-time insertion and compensation
- Hardware-synchronized multi-phase operation

The PIO-based implementation guarantees cycle-accurate timing independent of CPU load, ensuring consistent control loop performance even during communication bursts or diagnostic operations.

### Electronics Architecture

All chassis sizes share a common electronic architecture consisting of several key functional blocks. This modular approach enables reuse of control firmware, simplifies troubleshooting, and allows features to be developed once and deployed across the entire product family.

The primary functional blocks include the control board handling communication, user interface, and high-level control algorithms; the mainboard providing analog input handling, current sensing, and auxiliary interfaces; the gate driver board responsible for switching the power devices safely and efficiently; the snubber and EMI board providing voltage spike protection and electromagnetic interference filtering; and the bulk capacitor board storing the energy required for smooth operation across the DC link.

### Power Devices

The choice of power devices is determined by chassis size and application requirements:

**Size 0 (Robotics Drive):** Uses MOSFETs exclusively. These devices are selected for their low gate charge and fast switching characteristics, providing efficiency advantages at the lower voltages (12V-72V) typical of small robotics applications.

**Size 1 and Size 2 (Small and Standard EV Drive):** Use IGBT modules exclusively. These commercial modules integrate multiple IGBT chips in rugged packages rated for high current operation, providing superior voltage blocking capability and robust short-circuit performance essential for electric vehicle applications. The use of pre-packaged modules significantly simplifies mechanical and thermal design compared to paralleling discrete devices.

### Gate Driver Requirements

Gate drivers must provide sufficient peak current to switch power devices quickly (minimizing switching losses), maintain electrical isolation between the high-power and control circuits, and protect the power devices from fault conditions including over-current and over-temperature.

The gate driver designs used across all chassis sizes share common control interfaces and protection features, including desaturation protection for IGBTs, soft shutdown on fault, and isolated fault and ready status signals. This commonality allows firmware developed for one chassis size to be easily adapted for another.

### Communication Interface

CANbus serves as the primary communication interface across all chassis sizes. The implementation supports standard CAN 2.0B at 1 Mbps, providing sufficient bandwidth for real-time control and diagnostics. Future variants may support CAN FD for applications requiring higher bandwidth for firmware updates or detailed telemetry.

The communication protocol is documented separately and designed to be easily implemented in external controllers or host systems. This approach allows the inverter to integrate with a wide variety of vehicle architectures and control systems.

### Protection Features

Comprehensive protection features are implemented to ensure safe and reliable operation across all operating conditions. These include over-current protection using both hardware instantaneous limits and firmware-controlled current limiting, over-voltage and under-voltage protection on both the DC link and logic supply, over-temperature protection using multiple temperature sensors strategically placed throughout the design, and short-circuit protection with rapid shutdown capability.

---

## PCB Variant System

The platform uses a systematic approach to PCB variants that allows common designs to serve multiple configurations. Each PCB type has its own model number suffix convention that reflects its function and specifications.

| PCB Type | Code | Description |
|----------|------|-------------|
| Snubber Capacitor Board | SCB | Film caps, EMI filtering, voltage spike protection |
| Bulk Capacitor Board | BCB | Main DC link energy storage, voltage-rated variants |
| Gate Driver | GDR | Power device switching, isolation, protection |
| Main Control Board | MCB | Analog inputs, current sensing, auxiliary interfaces |
| Control Board | CTRL | Logic supply input, system control, communication |

### Bulk Capacitor Board Variants

The bulk capacitor board is the primary variant-dependent PCB within the platform, with different voltage ratings and capacitance values optimized for each voltage class. These variants are indicated on the silkscreen with checkboxes that can be marked during production.

| Variant | Maximum Voltage | Nominal Voltage | Class |
|---------|-----------------|-----------------|-------|
| 160V | 160V | 106V | LV |
| 200V | 200V | 132V | LV |
| 250V | 250V | 165V | LV |
| 350V | 350V | 230V | LV |
| 400V | 400V | 265N | HV |
| 450V | 450V | 300N | HV |

---

## Getting Started

### For Users

To use these inverter designs in your project, first identify the appropriate chassis size based on your power, voltage, and current requirements. Review the documentation for your chosen size to understand the mechanical integration requirements, thermal management recommendations, and control interface specifications.

The Bill of Materials (BOM) for each variant is provided in the respective directory. Components should be sourced according to the recommended suppliers and part numbers to ensure proper operation and compatibility. When assembling the inverter, pay careful attention to the isolation requirements, creepage and clearance distances, and thermal interface specifications.

### For Developers

Contributions to the project are welcome and encouraged. Before starting development, review the contribution guidelines and the roadmap to understand the current development priorities. The project uses KiCad for schematic and PCB design, and all source files are provided in the repository.

When contributing modifications or new designs, maintain consistency with the existing model numbering system, design conventions, and documentation standards. All designs should include comprehensive documentation including BOMs, assembly drawings, and operating instructions.

### Documentation Structure

The repository is organized by chassis size, with each size having its own directory containing the relevant documentation, PCB designs, BOMs, and technical specifications. Shared resources such as firmware, gate driver designs, and control board variants are located in the root-level directories.

