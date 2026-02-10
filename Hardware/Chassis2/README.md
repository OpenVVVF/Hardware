# Bill of Materials - Size 2 Chassis Inverter
## Complete Component Cost Breakdown

This is the cost for the following variant SKU: `C2LV106N600A-A4L12C`

---

## Assembly Summary

| Subassembly | Qty per Inverter | Parts Cost (each) | PCB Cost (each) | Assembly Cost (each) | Total per Inverter |
|-------------|------------------|-------------------|-----------------|----------------------|---------------------|
| Mechanical (excl. IGBTs) | — | $299.76 | — | — | $299.76 |
| **Snubber Board (Main Filter)** | **3** | **$47.82** | **$1.30** | **$4.33** | **$159.45** |
| Bulk Capacitor Board (BCB) | 3 | $26.52 | $1.30 | $1.00 | $86.46 |
| Gate Driver Board | 3 | $47.05 | $5.16 | $6.00 | $174.63 |
| Main Board | 1 | $105.91 | $9.78 | $15.00 | $130.69 |
| Control Board | 1 | $109.10 | $9.78 | $12.00 | $130.88 |
| **Subtotal (excl. IGBTs)** | — | — | — | — | **$982.87** |
| IGBT Modules | 3 | $192.63/ea | — | — | **$577.89** |
| **Total per Inverter** | — | — | — | — | **$1,560.76** |

---

## 1. Mechanical Components (Excluding IGBTs)

| Part Reference | Description | Material | Dimensions | Qty | Unit Price | Ext. Price |
|----------------|-------------|----------|------------|-----|------------|------------|
| InverterMechanical_BaseplateHeatspreader.step | Baseplate Heatspreader | 6061 T6 Aluminum (.375") | 6.693" × 12.402" | 1 | $142.90 | $142.90 |
| InverterMechanical_PhaseBusBar001.step | Phase Bus Bar | Copper (.187") | 0.591" × 3.167" | 3 | $17.37 | $52.10 |
| InverterMechanical_DCLinkBusBar.step | DC Link Bus Bar | Copper (.187") | 14.359" × 0.551" | 2 | $52.38 | $104.76 |
| | | | | | **Mechanical Subtotal** | **$299.76** |

---

## 2. IGBT Modules (Optional - Pricing TBD)

| Part Reference | Description | Manufacturer | Current | Voltage | Qty | Unit Price | Ext. Price |
|----------------|-------------|--------------|---------|---------|-----|------------|------------|
| FF600R12KE7BPSA1 | IGBT Module 62mm MEDIUM POWER | Infineon | 600A | 1200V | 6 | $192.63 | $1,155.78 |
| | | | | | | **IGBT Total** | **$1,155.78** |

> **Note:** IGBT module pricing is based on Infineon FF600R12KE7BPSA1. Alternative modules available at different price points. Total excludes IGBTs for flexibility in sourcing.

---

## 3. Snubber Board Assembly (3 per Inverter)

| Sort | Mouser Part # | Manufacturer | Description | Qty per Board | Unit Price | Ext. Price per Board | Est. Tariff |
|------|---------------|--------------|-------------|---------------|------------|----------------------|-------------|
| 1 | 75-MKP1848S61010JY5B | Vishay | Film Capacitor 10μF 1000V 5% | 2 | $7.54 | $15.08 | — |
| 2 | 72-VY1222M47Y5UQ6TV0 | Vishay | Safety Capacitor 2200pF X1 Y2 500VAC | 7 | $0.248 | $1.74 | $0.71 |
| 3 | 490-TBP01R2-508-04BE | Same Sky | Terminal Block Receptacle 4-Pole 5.08mm | 2 | $0.48 | $0.96 | $0.26 |
| 4 | 490-TBP01P1-508-04BE | Same Sky | Terminal Block Plug 4-Pole 5.08mm Blue | 3 | $0.833 | $2.50 | $0.68 |
| | | | | | **Parts Total per Board** | **$47.82** | **$1.65** |
| | | | | | **PCB Cost (10 for $13)** | | **$1.30** |
| | | | | | **Assembly (US Labor/Overhead per board)** | | **$4.33** |
| | | | | | **Snubber Board Total (1 board)** | | **$53.45** |
| | | | | | **Snubber Board Total (3 boards/inverter)** | | **$160.35** |

> **Note:** Current sensors removed from this assembly. Current sensing should be implemented via separate hall-effect sensors on phase outputs or DC link (not shown in this BOM).

---

## 4. Bulk Capacitor Board Assembly (3 per Inverter) — BCB

| Sort | Mouser Part # | Manufacturer | Description | Qty per Board | Unit Price | Ext. Price per Board | Est. Tariff |
|------|---------------|--------------|-------------|---------------|------------|----------------------|-------------|
| 1 | 647-UCS2C331MHD | Nichicon | Aluminum Electrolytic Cap 160V 330μF | 12 | $2.21 | $26.52 | $3.18 |
| | | | | | **Parts Total per Board** | **$26.52** | **$3.18** |
| | | | | | **PCB Cost (10 for $13)** | | **$1.30** |
| | | | | | **Assembly (US Labor/Overhead per board)** | | **$1.00** |
| | | | | | **BCB Total (1 board)** | | **$28.82** |
| | | | | | **BCB Total (3 boards/inverter)** | | **$86.46** |

---

## 5. Gate Driver Board Assembly (3 per Inverter)

| Sort | Mouser Part # | Manufacturer | Description | Qty per Board | Unit Price | Ext. Price per Board | Est. Tariff |
|------|---------------|--------------|-------------|---------------|------------|----------------------|-------------|
| 1 | 833-FM2000GP-TP | MCC | Rectifier 0.5A High Voltage Fast Recovery | 2 | $0.38 | $0.76 | — |
| 2 | 187-CL32A106KAULNNE | Samsung | MLCC 10μF 25V X5R 1210 | 14 | $0.125 | $1.75 | $0.63 |
| 3 | 603-RC1210FR-071KL | YAGEO | Resistor 1kΩ 1% 1/2W 1210 | 4 | $0.10 | $0.40 | $0.08 |
| 4 | 80-C1210C104K1RAC | KEMET | MLCC 0.1μF 100V X7R 1210 | 10 | $0.162 | $1.62 | — |
| 5 | 667-ERJ-P14J9R1U | Panasonic | Resistor 9.1Ω 0.5W 5% 1210 AEC-Q200 | 2 | $0.21 | $0.42 | — |
| 6 | 71-CRCW12104K70FKEAH | Vishay | Resistor 4.7kΩ 1% 3/4W AEC-Q200 | 7 | $0.55 | $3.85 | $0.54 |
| 7 | 584-ADUM4136BRWZ | Analog Devices | Isolated Half-bridge Gate Driver | 2 | $10.25 | $20.50 | — |
| 8 | 859-LTST-C230GKT | LITEON | LED Green 569nm Clear | 1 | $0.12 | $0.12 | $0.05 |
| 9 | 534-5000 | Keystone | Test Point Red | 6 | $0.32 | $1.92 | $0.29 |
| 10 | 647-UCM1H101MCL1GS | Nichicon | Alum. Electrolytic Cap 50V 100μF AEC-Q200 | 1 | $0.67 | $0.67 | $0.08 |
| 11 | 963-FBMJ1608HS220NTR | TAIYO YUDEN | Ferrite Bead 22Ω 0603 | 2 | $0.11 | $0.22 | — |
| 12 | 621-BAT54WS-7-F | Diodes Inc. | Schottky Diode 30V 200mW | 2 | $0.10 | $0.20 | $0.11 |
| 13 | 603-AC1210JR-070RL | YAGEO | Resistor 0Ω 5% 1/2W 1210 Auto Grade | 4 | $0.10 | $0.40 | $0.08 |
| 14 | 859-LTST-C230CKT | LITEON | LED Red 638nm Clear | 1 | $0.20 | $0.20 | $0.09 |
| 15 | 580-MGJ2D121509MPC-R7 | Murata | DC/DC Converter Isolated 2W 12-15/9V | 2 | $6.60 | $13.20 | — |
| 16 | 667-ERJ-T14J6R8U | Panasonic | Resistor 6.8Ω 5% 1210 AEC-Q200 | 2 | $0.36 | $0.72 | — |
| 17 | 603-RC1210FR-07220RL | YAGEO | Resistor 220Ω 1% 1/2W 1210 | 1 | $0.10 | $0.10 | $0.02 |
| | | | | | **Parts Total per Board** | **$47.05** | **$2.79** |
| | | | | | **PCB Cost (per board)** | | **$5.16** |
| | | | | | **Assembly (US Labor/Overhead per board)** | | **$6.00** |
| | | | | | **Gate Driver Board Total (1 board)** | | **$58.21** |
| | | | | | **Gate Driver Board Total (3 boards/inverter)** | | **$174.63** |

---

## 6. Main Board Assembly (1 per Inverter)

| Sort | Mouser Part # | Manufacturer | Description | Qty | Unit Price | Ext. Price | Est. Tariff |
|------|---------------|--------------|-------------|-----|------------|------------|-------------|
| 1 | 80-C1210C220J5G | KEMET | MLCC 22pF 50V C0G 1210 5% | 5 | $0.71 | $3.55 | — |
| 2 | 603-RC1210FR-071KL | YAGEO | Resistor 1kΩ 1% 1/2W 1210 | 9 | $0.10 | $0.90 | $0.18 |
| 3 | 863-SZMMSZ4678T1G | onsemi | Zener Diode 1.8V 0.5W | 8 | $0.11 | $0.88 | $0.15 |
| 4 | 534-5000 | Keystone | Test Point Red | 10 | $0.293 | $2.93 | $0.44 |
| 5 | 187-CL32A106KAULNNE | Samsung | MLCC 10μF 25V X5R 1210 | 12 | $0.125 | $1.50 | $0.54 |
| 6 | 859-LTST-C230CKT | LITEON | LED Red 638nm Clear | 1 | $0.20 | $0.20 | $0.09 |
| 7 | 595-ISO1042BDWVR | Texas Instruments | Isolated CAN Transceiver | 1 | $4.57 | $4.57 | — |
| 8 | 647-UCM1H101MCL1GS | Nichicon | Alum. Electrolytic Cap 50V 100μF | 12 | $0.376 | $4.51 | $0.54 |
| 9 | 71-CRCW1210-249K-E3 | Vishay | Resistor 249kΩ 1% 1/2W 1210 | 16 | $0.066 | $1.06 | — |
| 10 | 700-MAX22530AWE+ | Analog Devices | 4-Channel 12-bit ADC | 2 | $17.28 | $34.56 | — |
| 11 | 187-CL32B103KGFNNNE | Samsung | MLCC 10nF 500V X7R 1210 | 8 | $0.31 | $2.48 | $0.38 |
| 12 | 919-RKE-1205S/H | RECOM | DC/DC Converter 1W 5V SIP7 | 4 | $4.88 | $19.52 | $3.12 |
| 13 | 523-AWHSH105D00G | Amphenol | Relay SPDT 5VDC Wash Tight | 3 | $1.10 | $3.30 | $1.19 |
| 14 | 80-C1210C104K1RAC | KEMET | MLCC 0.1μF 100V X7R 1210 | 10 | $0.162 | $1.62 | — |
| 15 | 579-MCP2515-E/ST | Microchip | CAN Controller SPI | 1 | $3.05 | $3.05 | — |
| 16 | 710-830003156B | Wurth Elektronik | Crystal 8.0MHz 50ppm | 1 | $0.50 | $0.50 | $0.20 |
| 17 | 621-DRDNB21D-7 | Diodes Incorporated | Dual Relay Driver | 2 | $0.74 | $1.48 | $0.83 |
| 18 | 187-CL32B105KBHNNNE | Samsung | MLCC 1μF 50V X7R 1210 | 6 | $0.23 | $1.38 | $0.21 |
| 19 | 667-ERJ-P14J202U | Panasonic | Resistor 2kΩ 0.5W 5% 1210 | 4 | $0.21 | $0.84 | — |
| 20 | 810-ACT45B1012PTL003 | TDK | Common Mode Choke CAN-BUS | 1 | $1.40 | $1.40 | $0.21 |
| 21 | 490-TBP01R1-508-06BE | Same Sky | Terminal Block Receptacle 6-Pole | 1 | $0.68 | $0.68 | $0.18 |
| 22 | 919-REC10K-2424DAWH2 | RECOM | DC/DC Converter 10W ±24Vout | 1 | $12.13 | $12.13 | $4.37 |
| 23 | 667-ERJ-U14F5101U | Panasonic | Resistor 5.1kΩ 1% 1210 Anti-Sulfur | 4 | $0.38 | $1.52 | — |
| | | | | | **Parts Total** | **$105.91** | **$12.92** |
| | | | | | **PCB Cost (5 for $48.90)** | | **$9.78** |
| | | | | | **Assembly (US Labor/Overhead)** | | **$15.00** |
| | | | | | **Main Board Total** | | **$143.61** |

---

## 7. Control Board Assembly (1 per Inverter)

| Sort | Mouser Part # | Manufacturer | Description | Qty | Unit Price | Ext. Price | Est. Tariff |
|------|---------------|--------------|-------------|-----|------------|------------|-------------|
| 1 | 490-TBP02R2W-38106BE | Same Sky | Terminal Block Receptacle 6-Pole 3.81mm | 2 | $0.89 | $1.78 | $0.18 |
| 2 | 647-UCM1H101MCL1GS | Nichicon | Alum. Electrolytic Cap 50V 100μF | 42 | $0.376 | $15.79 | $1.89 |
| 3 | 534-5000 | Keystone | Test Point Red | 13 | $0.293 | $3.81 | $0.57 |
| 4 | 863-SZMMSZ4678T1G | onsemi | Zener Diode 1.8V 0.5W | 8 | $0.11 | $0.88 | $0.15 |
| 5 | 538-22-28-4022 | Molex | Header Vertical 2P | 1 | $0.28 | $0.28 | — |
| 6 | 737-RD7-12S033R | Adam Tech | DC/DC Converter 3W 3.3V Isolated | 1 | $5.63 | $5.63 | $0.79 |
| 7 | 700-MAX22530AWE+ | Analog Devices | 4-Channel 12-bit ADC | 2 | $17.28 | $34.56 | — |
| 8 | 187-CL32B103KGFNNNE | Samsung | MLCC 10nF 500V X7R 1210 | 8 | $0.31 | $2.48 | $0.38 |
| 9 | 490-TBP02R2W-38108BE | Same Sky | Terminal Block Receptacle 8-Pole 3.81mm | 2 | $1.19 | $2.38 | $0.24 |
| 10 | 859-LTST-C230CKT | LITEON | LED Red 638nm Clear | 2 | $0.20 | $0.40 | $0.18 |
| 11 | 603-RC1210FR-071KL | YAGEO | Resistor 1kΩ 1% 1/2W 1210 | 10 | $0.025 | $0.25 | $0.05 |
| 12 | 919-RKE-1205S/H | RECOM | DC/DC Converter 1W 5V | 2 | $4.88 | $9.76 | $1.56 |
| 13 | 709-SCW20A-12 | MEAN WELL | DC/DC Converter 20W 12V Isolated | 1 | $22.00 | $22.00 | $3.96 |
| 14 | 512-S3N | onsemi | Rectifier 3A 1200V SMD | 2 | $0.26 | $0.52 | $0.09 |
| 15 | 80-C1210C104K1RAC | KEMET | MLCC 0.1μF 100V X7R 1210 | 9 | $0.27 | $2.43 | — |
| 16 | — | Raspberry Pi | Raspberry Pi Pico (to be sourced separately) | 1 | TBD | TBD | — |
| 17 | 490-TBP01R2W-50802BE | Same Sky | Terminal Block Receptacle 2-Pole 5.08mm | 1 | $0.49 | $0.49 | $0.05 |
| 18 | 490-TBP01R1-508-04BE | Same Sky | Terminal Block Receptacle 4-Pole 5.08mm | 1 | $0.17 | $0.17 | $0.05 |
| 19 | 187-CL32A106KAULNNE | Samsung | MLCC 10μF 25V X5R 1210 | 1 | $0.29 | $0.29 | $0.10 |
| 20 | 187-CL32B105KBHNNNE | Samsung | MLCC 1μF 50V X7R 1210 | 6 | $0.23 | $1.38 | $0.21 |
| 21 | 603-AC1210JR-070RL | YAGEO | Resistor 0Ω 5% 1/2W 1210 Auto Grade | 1 | $0.10 | $0.10 | $0.02 |
| 22 | 667-ERJ-U14F5101U | Panasonic | Resistor 5.1kΩ 1% 1210 Anti-Sulfur | 8 | $0.38 | $3.04 | — |
| 23 | 652-MF-RHT200/32-2 | Bourns | PTC Resettable Fuse 2A 32V | 1 | $0.58 | $0.58 | $0.16 |
| 24 | 603-AC1210JR-0710KL | YAGEO | Resistor 10kΩ 5% 1/2W 1210 | 1 | $0.10 | $0.10 | $0.02 |
| | | | | | **Parts Total** | **$109.10** | **$10.60** |
| | | | | | **PCB Cost (5 for $48.90)** | | **$9.78** |
| | | | | | **Assembly (US Labor/Overhead)** | | **$12.00** |
| | | | | | **Control Board Total** | | **$142.48** |

---

## PCB Cost Summary

| Board Type | Qty per Inverter | Batch Details | Cost per Board | Total per Inverter |
|------------|------------------|---------------|----------------|---------------------|
| Snubber Board (Main Filter) | 3 | 10 for $13 | $1.30 | $3.90 |
| Bulk Capacitor Board (BCB) | 3 | 10 for $13 | $1.30 | $3.90 |
| Gate Driver Board | 3 | Volume production | $5.16 | $15.48 |
| Main Board | 1 | 5 for $48.90 | $9.78 | $9.78 |
| Control Board | 1 | 5 for $48.90 | $9.78 | $9.78 |
| **Total PCB Cost** | | | | **$42.84** |

---

## Additional Costs (Not Included)

| Item | Estimated Cost | Notes |
|------|----------------|-------|
| 3D Printed Enclosure/Shells | TBD | To be quoted separately |
| Standoffs and Hardware | < $75 | Est. upper limit |
| Wiring and Interconnects | TBD | To be added |
| Thermal Interface Materials | TBD | To be added |
| **Current Sensors** (Hall Effect) | TBD | 3x phase + 1x DC link recommended; removed from Snubber Board |
| | | |
| **Estimated Additional** | **<$75** | Excluding 3D printed parts and current sensors |
