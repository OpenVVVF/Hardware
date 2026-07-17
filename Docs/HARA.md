---
doctype: Hazard Analysis & Risk Assessment
title: Traction Inverter
mcus: STM32H723ZG + STM32G474RCTx
temp: −40 °C to +85 °C
version: "4.1"
prepared: Thomas Liao
reviewed: (not yet reviewed)
date: July 13, 2026
---

# 1. Introduction

## 1.1 Compliance Statement

> **This Is Not an ISO 26262 Compliance Claim**
>
> This document **applies ISO 26262:2018 methodology** (Part 3: Concept Phase) to identify hazards, assess risk via Severity/Exposure/Controllability classification, and derive Safety Goals with ASIL ratings. The following must be understood clearly:
>
> - **ASIL ratings are targets** derived from the HARA process. They represent the assessed risk level of identified hazardous events, **not a claim that the design has been verified or certified to meet those ASILs**.
> - **The system described is not ISO 26262 compliant.** Full compliance would require: complete product development per Parts 4-6, hardware architectural metrics (SPFM ≥ 90% for ASIL B, ≥ 97% for ASIL D; LFM ≥ 60% for ASIL B, ≥ 80% for ASIL D), dependent failure analysis (DFA), software tool qualification, verification and validation testing (including fault injection), configuration management, change control, and independent safety assessment. **None of these have been completed.**
> - The hardware implements a **dual-MCU architecture**: STM32H723ZG main MCU + STM32G474RCTx safety coprocessor. The coprocessor provides independent ADC monitoring of all safety-critical signals, 1oo2 gate drive power kill, challenge/response watchdog, independent CAN bus snooping, and bidirectional NRST. This makes **ASIL D achievable for SG-01 and SG-13 via ASIL B(D) + ASIL B(D) decomposition**.
> - The gate driver ICs (onsemi NCV57100) are **automotive-qualified (AEC-Q100)** but are **not ISO 26262 safety elements**. No safety manual, FMEDA, or ASIL claim is available from the manufacturer. Internal protections (DESAT, anti-shoot-through, UVLO) provide hardware-level risk reduction but cannot be claimed as ASIL-rated safety mechanisms without additional justification.
> - **Software Test Library (STL) limitation:** This project uses ST's publicly available X-CUBE-CLASSB library (IEC 60730-1 Class B certified). The ISO 26262-certified Class D STL (X-CUBE-STL) requires an NDA and is not available for open-source use. The following ASIL process gaps result: no FMEDA/SPFM/LFM metrics derived for this hardware configuration; no ISO 26262 software tool qualification (compiler, static analysis); no fault injection testing campaign with coverage evidence; no independent safety assessment. ASIL decomposition using the Class B STL is a design and educational exercise only.
> - The firmware is **architecturally complete**. Field-Oriented Control (FOC) is implemented. Safety mechanisms (tractive effort plausibility checking, fault ramp-down, boot CRC, challenge/response watchdog) are specified in this document; their implementation status is tracked in Sections 8 and 11.
> - This HARA and its accompanying Fault Injection Test Plan are **living design-input documents** intended to guide development and establish a safety engineering baseline. They do not constitute a product safety case, compliance certification, or warranty of fitness for any purpose.
>
> This is an **open-source aftermarket traction inverter** project. The documentation is published in the interest of transparency. Users bear full responsibility for evaluating the suitability of this design for their specific application, risk tolerance, and applicable regulatory requirements.

## 1.2 Scope

This document presents the Hazard Analysis and Risk Assessment (HARA) and Fault Injection Test Plan for a combined traction inverter and Vehicle Control Unit (VCU) designed to drive 3-phase PMSM traction motors from a DC link bus supply. The primary application focus is high-performance electric motorcycles (aftermarket replacement for Zero, Energica, and similar platforms), though the traction inverter may be used with any compatible DC link bus supply and 3-phase traction motor.

The DC link is supplied from a traction battery pack with Battery Management System (BMS), though the traction inverter itself is agnostic to the DC source type provided the voltage remains within the specified operating window. The analysis covers the traction inverter/VCU unit and its interfaces to external systems that affect safety.

The scope includes all hardware and software within the combined unit: the STM32H723ZG main MCU, the STM32G474RCTx safety coprocessor, six NCV57100 gate drivers, 3-phase IGBT power stage, current/voltage/temperature sensing, HVIL circuit, tractive effort control input processing, motor control algorithm, CAN communication, inter-MCU challenge/response watchdog, and fault handling. The Safety Coprocessor is **part of the current design** and provides independent monitoring, 1oo2 gate drive power kill, and ASIL B(D) decomposition.

**Out of scope:** The traction motor itself (external product), the rotor position sensor/encoder (part of the external motor), the Battery Management System (BMS), the IO board, the charger, and the vehicle display — these are external CAN nodes interfaced by the VCU but not designed or manufactured by this project. The CAN protocol definitions in this document are the VCU-side interface only.

> **ASIL Decomposition via Dual MCU**
>
> This design implements **ASIL B(D) decomposition** through two independent MCUs: the STM32H723ZG (main processor) and the STM32G474RCTx (safety coprocessor). Each MCU independently monitors all safety-critical signals. Either MCU can trigger safe state entry. The 1oo2 gate drive power kill (GATE_DRIVE_PWR1_ENABLE from main, GATE_DRIVE_PWR2_ENABLE from coprocessor) provides an independent supply shutdown path. Target ASIL D is achievable for SG-01 and SG-13 via ASIL B(D) + ASIL B(D) decomposition.

## 1.3 Reference Standards

| Standard | Title | Application |
| --- | --- | --- |
| ISO 26262-1:2018 | *Road vehicles — Functional Safety — Part 1: Vocabulary* | Definitions and abbreviations |
| ISO 26262-3:2018 | *Part 3: Concept phase* | HARA methodology, Safety Goals, FSR derivation |
| ISO 26262-4:2018 | *Part 4: Product development at the system level* | Technical Safety Requirements, system design |
| ISO 26262-5:2018 | *Part 5: Product development at the hardware level* | Hardware architectural metrics (SPFM, LFM) |
| ISO 26262-8:2018 | *Part 8: Supporting processes* | Test planning, change management |
| ISO 26262-9:2018 | *Part 9: ASIL-oriented and safety-oriented analyses* | ASIL decomposition, safety analysis |
| ISO 6469-3:2018 | *Electrically propelled road vehicles — Safety specifications — Part 3: Electrical safety* | HV isolation requirements |
| EN 50155 | *Railway applications — Electronic equipment used on rolling stock* | Onboard power supply qualification reference |

# 2. Item Definition

## 2.1 System Boundaries

**Table 1 — System Boundary Inclusions and Exclusions**

| Category | Description |
| --- | --- |
| **In Scope** | Combined traction inverter/VCU PCB, six onsemi NCV57100 isolated gate drivers (non-ASIL), 3-phase 2-level IGBT bridge, **STM32H723ZG main MCU + STM32G474RCTx safety coprocessor** (dual independent core), phase current sensing (3-phase + DC link), DC link bus voltage sensing, phase voltage sensing, IGBT temperature sensing (3 modules, 1 NTC per module), traction motor temperature sensing, traction motor encoder input, HVIL circuit, dual redundant tractive effort control input + end-travel limit switch, CAN1 (BMS), CAN2 (ABS, display, charger, IO board), precharge control, fault handling logic, 1oo2 gate drive power supply kill (GATE_DRIVE_PWR1_ENABLE + GATE_DRIVE_PWR2_ENABLE), inter-MCU challenge/response watchdog, CY15B102Q-SXET 256 KB FRAM (main MCU side) |
| **Interfaced (external)** | Traction battery pack with BMS (provides DC link, CAN1 heartbeat 5 s), ABS module (CAN2, independently powered), display/dash (CAN2), charger(s) (CAN2), IO board (CAN2, brake switch, kickstand switch, turn signal feedback, headlight feedback, 1 s heartbeat), 3-phase PMSM traction motor with encoder, 12 V onboard power (EC7BW-110S12 DC/DC from DC link) |
| **Out of Scope** | BMS internal cell protection and contactor control, ABS hydraulic/mechanical system, charger AC-side circuitry, vehicle chassis, traction motor construction, DC link source beyond electrical interface |

## 2.2 Architecture Overview

The traction inverter/VCU is a combined unit that performs both motor control (inverter) and vehicle-level control (VCU) functions. The architecture is organized into three functional layers: the primary control path, the hardware protection layer, and the safe state actuation layer.

**Table 2 — System Architecture**

| **PRIMARY CONTROL PATH — STM32H723ZG MAIN MCU** |  |  |
| --- | --- | --- |
| **Core Processing**<br>550 MHz Cortex-M7<br>ECC RAM<br>Brown-out detect<br>Internal WDT<br><br>**Software Functions:**<br>FOC motor control<br>Tractive effort command processing<br>Sensor acquisition & plausibility<br>Fault detection & handling<br>CAN communication (FDCAN1 + FDCAN2)<br>Safe state management<br>Gate drive power kill (Path 2a)<br><br>**Coprocessor interface:**<br>Inter-MCU UART<br>Timer sync line<br>Bidirectional NRST<br><br>**Storage:**<br>CY15B102Q-SXET 256 KB FRAM (SPI) for fault logs, configuration, hour meter, odometer. Hardware WP pin. | **Interface** | **Connected System** |
|  | DC link (102–350 V) | Traction battery (or compatible DC source) |
|  | 3-Phase AC via NCV57100 x6 | PMSM traction motor |
|  | CAN1 (heartbeat 5 s) | BMS |
|  | CAN2 (heartbeat 1 s) | IO board (brake, kickstand, signals) |
|  | CAN2 | ABS module, display, charger |
|  | Analog (dual pot + limit switch) | Tractive effort control input |
|  | Sin/Cos analog / Hall effect | Traction motor encoder |
|  | EC7BW-110S12 | 12 V onboard power rail |
|  | FLT (OR'd, active low) | Six NCV57100 fault outputs |
| **HARDWARE PROTECTION LAYER — NCV57100 GATE DRIVERS (NON-ASIL)** |  |  |
| Six NCV57100 devices provide **local hardware protection** for each IGBT: DESAT short-circuit detection (<2 us), complementary anti-shoot-through inputs, UVLO, active Miller clamp, soft turn-off, gate active pull-down. These protections operate independently of the STM32 and provide the first line of defense against power stage faults. All six FLT outputs are OR'd together and fed to the STM32 fault input. **Note: these protections are not ASIL-rated but provide valuable hardware-level risk reduction.** |  |  |
| **INDEPENDENT SAFETY MONITOR — STM32G474RCTx SAFETY COPROCESSOR** |  |  |
| **Core:** 170 MHz Cortex-M4+FPU, 8 MHz crystal, shared +3.3 V rail (dedicated RD7-12S033R DC/DC converter), independent oscillator.<br>**Independent ADC access:** All 4 current sense signals (phase U/V/W + DC link) + REF, all 3 heatsink temperature sensors + motor temp, all encoder signals (Hall U/V/W, Sin/Cos).<br>**Independent gate drive monitoring:** GATE_DRIVE_FAULT (OR'd FLT), GATE_DRIVE_READY, GATE_DRIVE_RESET, GATE_DRIVE_PWR1_FEEDBACK, GATE_DRIVE_PWR2_FEEDBACK. All 6 PWM outputs monitored (PH_U/V/W_HIGH/LOW).<br>**Independent CAN:** FDCAN2 + FDCAN3 — can snoop both CAN buses to cross-check torque commands and node heartbeats.<br>**Inter-MCU communication:** Dedicated UART + timer sync line + bidirectional NRST cross-reset.<br>**1oo2 gate drive power kill:** GATE_DRIVE_PWR2_ENABLE (coprocessor) in logical-OR with GATE_DRIVE_PWR1_ENABLE (main). Either MCU deasserting its enable kills all six gate drive supplies. Each enable has independent feedback (GATE_DRIVE_PWR1_FB, GATE_DRIVE_PWR2_FB).<br>**Challenge/response watchdog:** Coprocessor issues challenge; main MCU must respond within window. Failure → coprocessor asserts NRST on main MCU → SSO.<br>**Safe state authority:** Coprocessor can independently trigger SSO via gate drive power kill, gate drive RESET, or main MCU NRST. |  |  |
| **RAIL SUPERVISOR — TPS389006-Q1 6-CHANNEL WINDOW SUPERVISOR** |  |  |
| A TPS389006-Q1 six-channel window supervisor monitors the +3.3 V, +5 V, +12 V, and sensor +5 V rails plus both gate-drive power feedbacks (GATE_DRIVE_PWR1_FB, GATE_DRIVE_PWR2_FB). Window thresholds are I2C-configured by the main MCU at boot. On any out-of-window rail fault — including brownout of the +3.3 V rail shared by both MCUs — its NIRQ output asserts the shared GATE_DRIVER_FAULT line, which is monitored by both MCUs (the same net that carries the OR'd gate-driver FLT). The device is TI Functional Safety-Compliant and supports designs up to SIL 3 / ASIL D per TI. The boot-time I2C threshold configuration is a dependency to be covered by the pending DFA (LIMIT-08). |  |  |
| **SAFE STATE ACTUATION LAYER — SIX REDUNDANT SSO PATHWAYS** |  |  |
| **Path 1 (hardware, <100 ns):** TIM1 break input (TIM1_BKIN) → hardware clears MOE, all PWM outputs disabled. Triggered by OR'd gate driver FLT (DESAT/UVLO) or software fault. Independent of both CPU states after trigger.<br><br>**Path 2a (active, ~10 us):** Main MCU → GATE_DRIVE_PWR1_ENABLE low → all six Murata MGJ2D121509MPC-R7 supplies shut down → NCV57100 UVLO → active pull-down → SSO. Feedback via GATE_DRIVE_PWR1_FB.<br><br>**Path 2b (active, ~10 us):** Coprocessor → GATE_DRIVE_PWR2_ENABLE low → same supply shutdown. Independent of Path 2a. Feedback via GATE_DRIVE_PWR2_FB. Either Path 2a or 2b alone achieves SSO (1oo2). The ~10 us figures are actuation-only; time-to-SSO on the power-kill paths additionally depends on gate-bias rail decay to the NCV57100 UVLO threshold and is under characterization.<br><br>**Path 3 (hardware, passive):** Loss of shared 3.3V rail → NCV57100 VDD lost → internal active pull-down → SSO. Automatic, no software intervention.<br><br>**Path 4 (active, <1 us):** Either MCU → DRIVER_RESET asserted → all NCV57100 RESET inputs → outputs immediately disabled (hard turn-off via OUTL active pull-down; on the NCV57100, soft turn-off exists only on the DESAT path) → SSO. Both MCUs share the RESET line (either can assert). The TPS389006-Q1 rail supervisor is an additional fault source on this pathway: on any monitored-rail fault, including brownout of the +3.3 V rail shared by both MCUs, it asserts the shared GATE_DRIVER_FAULT line, signaling both MCUs to assert DRIVER_RESET.<br><br>**Path 5 (active, ~100 ms):** Coprocessor challenge/response watchdog failure → coprocessor asserts main MCU NRST → system reset → SSO during boot. Main MCU WDT timeout as backup.<br><br>**Path 6 (active, <10 us):** Coprocessor detects critical fault independently → asserts GATE_DRIVE_PWR2_ENABLE low + GATE_DRIVE_RESET → SSO without relying on main MCU. |  |  |

## 2.3 Mitigation Strategy

The following table explains how each hazard class is mitigated by the architecture, which mechanisms cover which failure modes, and where the known limitations are.

**Table 3 — Hazard Mitigation Strategy**

| Hazard Category | Failure Mode | Mitigation Mechanism | Limitation |
| --- | --- | --- | --- |
| **Unintended tractive effort** (H-01, H-15) | Throttle sensor fault (open, short, drift) | Dual redundant pots with >5% discrepancy check (FSR-01) on both MCUs; throttle limit switch override (FSR-18); rate limiter (FSR-03) | Both MCUs independently read throttle; either detecting discrepancy triggers SSO via independent power kill |
| **Unintended tractive effort** (H-01, H-15) | Software error in tractive effort calculation | Torque command vs. measured current plausibility (FSR-02); boot CRC (FSR-19); ECC RAM (FSR-20); windowed WDT (FSR-15) | Plausibility check is software-based; common-cause with main control possible |
| **Unintended tractive effort** (H-01, H-15) | MCU latch-up / runaway | Windowed watchdog ≤50 ms (FSR-15); breakpoint HW PWM disable (FSR-14) | WDT is on-chip; common-cause with MCU failure possible |
| **Loss of tractive effort** (H-03, H-03a) | Fault-triggered safe state entry | Torque ramp-down at ≤200 Nm/s before SSO (FSR-05); gradual removal prevents destabilization | Implemented via controlled deceleration profile |
| **Loss of tractive effort** (H-03, H-03a) | External system loss (CAN, BMS) | CAN heartbeat timeouts with safe defaults (FSR-17); graceful degradation | IO board loss → zero tractive effort (safe but abrupt if rider not expecting) |
| **Over-torque** (H-06) | Excessive tractive effort command | Software torque limit LUT (FSR-07); torque command plausibility (FSR-02); coprocessor independent current monitoring with cross-check | DESAT handles hard short-circuit (<2 us). Regular overcurrent detected within 100 ms by dual-MCU integrated monitoring — sufficient for safe state off without hardware damage. No separate HW OCP comparator required. |
| **Over-torque** (H-06) | Short-circuit / shoot-through | NCV57100 DESAT (<2 us) (FSR-13); complementary anti-shoot-through inputs (FSR-12); active Miller clamp | Gate drivers are non-ASIL; coprocessor independently monitors all 6 PWM output pairs for deadtime violations and stuck-on/stuck-off |
| **Over-temperature** (H-07) | IGBT thermal runaway | 3 IGBT modules, 1 NTC sensor per module, 2-out-of-3 voting with 100 °C hard cap (FSR-08); progressive derating; critical threshold → SSO | 2oo3 voter implemented; stuck-high sensor can cause unnecessary derating (known trade-off: safety over availability) |
| **Encoder loss** (H-08) | Loss of rotor position feedback | Encoder timeout detection <100 ms (FSR-09); immediate SSO on loss | Single encoder (no redundancy); bounded sensorless fallback if implemented |
| **DC link overvoltage** (H-10) | Regen-induced bus rise | Isolated ADC monitoring (FSR-11); regen disable at warning threshold; SSO at critical threshold | None significant |
| **HV isolation** (H-09, H-11) | HV exposure / contactor weld | HVIL continuous monitoring (FSR-10); interruption → PWM disable + contactor open request | Contactor control is in BMS domain; VCU can only request |
| **Safe state failure** (H-13, H-14) | Cannot reach SSO; latched tractive effort | Six redundant SSO pathways (Path 2a and Path 2b are redundant channels of one 1oo2 power-kill pathway): Path 1 = TIM1_BKIN hardware (<100 ns); Path 2a/2b = 1oo2 gate-drive power kill (GATE_DRIVE_PWR1_ENABLE / GATE_DRIVE_PWR2_ENABLE); Path 3 = shared 3.3 V rail loss → NCV57100 pull-down; Path 4 = GATE_DRIVE_RESET; Path 5 = coprocessor watchdog → NRST; Path 6 = coprocessor independent fault trigger. WDT reset (FSR-15); POST before PWM enable (FSR-16). | 1oo2 power kill: either GATE_DRIVE_PWR1_ENABLE or GATE_DRIVE_PWR2_ENABLE going low achieves SSO. Each has independent feedback (GATE_DRIVE_PWR1_FB, GATE_DRIVE_PWR2_FB). Coprocessor provides fully independent safe state actuation. Six pathways provide extensive redundancy against any single-point failure. |
| **Gate driver fault** (H-16, H-17) | DESAT/UVLO not detected; PWM deadtime violation | OR'd FLT input to STM32 (FSR-13); DESAT self-test at POST (FSR-16); complementary inputs (FSR-12) | OR'd FLT monitored by both MCUs; coprocessor additionally monitors the combined READY signal and all 6 PWM outputs for independent fault diagnosis |

> **How to read this table:** Each row maps a hazard category to the specific failure modes that could cause it, the mitigation mechanisms in the current architecture that address those failure modes, and the known limitations of those mitigations. The left column is the **what could go wrong**; the middle column is the **how we prevent or detect it**; the right column is the **why this might not be enough**. The limitations are addressed in Section 9 (Gap Analysis).

## 2.4 Technical Parameters

**Table 4 — Key Technical Parameters**

| Parameter | Value / Range |
| --- | --- |
| Vehicle type | Electric 2-wheel motorcycle (aftermarket) |
| Target platforms | Zero (102 V nom), Energica (<320 V), similar |
| Max traction motor power | 100 kW peak (200+ kW possible with derating) |
| Max vehicle speed | 150 mph (240 km/h) |
| Vehicle kerb weight | Up to 600 lbs (272 kg) |
| Traction motor type | 3-phase PMSM, FOC controlled |
| DC link voltage range | 48 V – 350 V (nominal 102 V – 320 V) |
| DC link source | Traction battery with BMS (or compatible DC source) |
| Main MCU | STM32H723ZG, 550 MHz Cortex-M7, ECC RAM, FDCAN1/2, HRTIM |
| Gate driver | onsemi NCV57100 x6 (AEC-Q100, non-ASIL) |
| Isolation | >5 kV<sub>rms</sub> (reinforced) per channel |
| Power stage | 3-phase 2-level IGBT bridge |
| Onboard DC/DC | Cincon EC7BW-110S12 (railway grade, EN 50155) |
| Fail-safe default | Six-switch-open (SSO) via NCV57100 active pull-down |
| Gate kill paths | Six redundant SSO pathways (TIM1_BKIN, 1oo2 gate-drive power kill, shared 3.3 V rail loss, GATE_DRIVE_RESET, coprocessor NRST, coprocessor independent trigger) |
| FLT outputs | All six NCV57100 FLT OR'd to fault input read by both MCUs |
| Regenerative braking | One-pedal (tractive effort control rollback), not wheel-lock capable |

## 2.5 Gate Drivers (Non-ASIL)

Six **onsemi NCV57100** isolated high-current IGBT gate drivers. Automotive-qualified per AEC-Q100. These devices are **not ISO 26262 safety elements** — no safety manual, FMEDA, or ASIL claim is available.

**Table 5 — NCV57100 Safety-Relevant Features**

| Feature | Specification | Safety Role | ASIL Credit |
| --- | --- | --- | --- |
| Reinforced isolation | >5 kV<sub>rms</sub>, 1200 V working | HV-to-logic barrier | Hardware only |
| Complementary inputs (IN+/IN−) | Internal anti-shoot-through logic | Prevents HS+LS simultaneous ON | None (not ASIL-rated) |
| DESAT protection | V<sub>TH</sub> = 6.5 V, prog. blanking | Short-circuit detection every ON cycle | None (not ASIL-rated) |
| Soft turn-off | Controlled slope on DESAT | Limits di/dt overvoltage | None (not ASIL-rated) |
| Active Miller clamp | Internal N-FET | Prevents dV/dt turn-on | None (not ASIL-rated) |
| Gate active pull-down | OUT pulled low on fault/UVLO | Ensures IGBT OFF | None (not ASIL-rated) |
| UVLO | V<sub>UVLO+</sub> = 12.2 V, V<sub>UVLO−</sub> = 11.3 V | No operation at low gate drive | None (not ASIL-rated) |
| Negative gate drive | VEE2 to −9 V | Robust OFF-state | Hardware only |
| FLT output | Open-drain, active low | Fault reporting to MCU | OR'd; monitored by both MCUs |

> **Non-ASIL Gate Driver Implications:** The NCV57100 internal protections provide valuable hardware-level risk reduction but cannot be counted toward ASIL metrics. The OR'd FLT output is monitored by **both** the main MCU (via TIM1_BKIN) and the coprocessor (via independent GPIO). Either MCU detecting FLT can trigger SSO. The coprocessor additionally monitors the combined NCV57100 READY output and can detect a stuck-active FLT line through its independent PWM output monitoring (all 6 phase high/low signals). This dual monitoring closes the single-point FLT path gap.

## 2.6 Onboard Power

**Table 6 — EC7BW-110S12 Parameters**

| Parameter | Specification |
| --- | --- |
| Input voltage | 40–160 VDC (4:1 range) |
| Output | 12 V / 1.67 A (20 W) |
| I/O isolation | 1500 VDC |
| Protection | OTP, OCP, OVP, UVLO |
| Qualification | EN 50155, EN 45545-2, IEC/EN/UL 62368-1 |
| Temperature | −40 °C to +105 °C case |

## 2.7 Future Considerations

> **Safety Coprocessor — STM32G474RCTx (Implemented)**
>
> The Safety Coprocessor is a **STM32G474RCTx** (170 MHz Cortex-M4+FPU, 3x FDCAN, advanced motor-control timers) that operates as an **independent safety monitor** alongside the main STM32H723ZG. It is part of the current design and provides:
>
> - **Independent gate driver supply kill** (GATE_DRIVE_PWR2_ENABLE, Path 2b) — 1oo2 with main MCU's GATE_DRIVE_PWR1_ENABLE. Independent feedback via GATE_DRIVE_PWR2_FB.
> - **Independent ADC monitoring** of all current sensors, temperature sensors, and encoder signals via voltage divider networks
> - **Challenge-response watchdog** with the main STM32 via inter-MCU UART
> - **Independent PWM output monitoring** — all 6 phase high/low signals monitored for deadtime violations, stuck-on, stuck-off
> - **Independent gate driver FLT monitoring** via OR'd fault line + combined READY signal
> - **Independent CAN bus snooping** via FDCAN2 + FDCAN3 — cross-checks torque commands and heartbeat timing
> - **Bidirectional NRST** — coprocessor can reset main MCU; main MCU can reset coprocessor
>
> The coprocessor enables **target ASIL D claims for SG-01 and SG-13 via ASIL B(D) + ASIL B(D) decomposition**. Both MCUs must independently agree that operation is safe; either can trigger SSO.

# 3. Operational Situations

**Table 7 — Operational Situations for HARA**

| ID | Operational Situation | Description | Speed |
| --- | --- | --- | --- |
| OS-01 | Stationary, system active | Vehicle stopped, key-on, rider present, in gear | 0 mph |
| OS-02 | Creep / low speed | Parking lot, driveway, traffic jam | 0–10 mph |
| OS-03 | Urban driving | City streets, intersections, moderate traffic | 10–45 mph |
| OS-04 | Rural / arterial road | Secondary roads, moderate curves | 45–65 mph |
| OS-05 | Highway cruising | Multi-lane highway, straight, dense traffic | 65–85 mph |
| OS-06 | High speed highway | High-speed lane, limited maneuvering space | 85–150 mph |
| OS-07 | Hard acceleration | Wide open throttle, max tractive effort request | Variable |
| OS-08 | Regenerative braking | Tractive effort control rollback, one-pedal regen active | Variable |
| OS-09 | Combined braking | Mechanical brake + regen blend | Variable |
| OS-10 | Cornering / lean | Banked turn, leaned over, on tractive effort | Variable |
| **OS-10a** | **Cornering at limit / racetrack** | **High lean angle, maximum traction demand, rider relying on tractive effort to hold line** | **Variable (high)** |
| OS-11 | Power-on sequence | Precharge, system initialization | 0 mph |
| OS-12 | Power-off / shutdown | Key-off, DC link discharge, contactor open | 0 mph |
| OS-13 | Rain / wet road | Reduced traction, any speed | Variable |
| OS-14 | High ambient temperature | Desert operation, >40 °C ambient | Variable |
| OS-15 | Low traction surface | Gravel, sand, wet leaves, painted lines | Variable |

> **OS-10a (Cornering at Limit):** This situation is distinct from OS-10 (general cornering) because on a racetrack or spirited canyon road, the rider is at high lean angle with the rear tire at or near its traction limit. The rider is actively using tractive effort to modulate the cornering line. A sudden complete loss of tractive effort in this situation is not merely an inconvenience — it causes the motorcycle to stand up and run wide, potentially off the track/road surface. Recovery is extremely difficult at full lean. This situation elevates the controllability rating for loss-of-power hazards from C2 to **C3**.

### Environmental Conditions

**Table 8 — Environmental Operating Conditions**

| Parameter | Range |
| --- | --- |
| Ambient temperature | −20 °C to +50 °C |
| Humidity | 0% to 100% (condensing) |
| Road surface | Dry, wet, standing water, gravel, sand |
| Altitude | 0–3,000 m MSL |
| Vibration / shock | Motorcycle-mounted, high vibration |

# 4. Hazard Identification (HAZID)

Hazards are identified via systematic Functional Hazard Analysis (FHA) combining top-down FMEA perspective, expert judgment on power electronics and motorcycle dynamics, and ISO 26262 hazard category checklists. All hazards are stated at the **vehicle level** (harm to occupants or other road users).

**Table 9 — Identified Hazards**

| ID | Malfunctioning Behavior | Vehicle-Level Hazard | Affected Road Users |
| --- | --- | --- | --- |
| **H-01** | Unintended positive tractive effort production not requested by rider | Unintended acceleration, loss of vehicle control | Rider, passengers, other road users |
| **H-02** | Unintended reverse tractive effort | Unexpected rearward motion, tip-over | Rider, nearby pedestrians |
| **H-03** | Sudden loss of tractive effort (inability to produce requested tractive effort) | Unexpected loss of drive, rear collision risk | Rider, following vehicles |
| **H-03a** | **Loss of tractive effort during cornering at lean** | **Motorcycle stands up and runs wide off road/track; loss of directional control at full lean** | **Rider** |
| **H-04** | Inability to produce requested regenerative braking tractive effort | Extended stopping distance | Rider, following vehicles |
| **H-05** | Unintended regenerative braking tractive effort (uncommanded) | Unexpected deceleration, loss of stability | Rider, following vehicles |
| **H-06** | Excessive tractive effort exceeding design limits | Wheel spin, loss of traction, component damage | Rider |
| **H-07** | Failure to limit tractive effort during over-temperature | Fire, thermal damage, burn injury | Rider, nearby persons |
| **H-08** | Motor overspeed due to loss of rotor position feedback | Loss of FOC control, unpredictable tractive effort | Rider |
| **H-09** | HV electrical isolation failure | Electric shock, potentially fatal | Rider, service personnel |
| **H-10** | DC link bus overvoltage not detected / not limited | Component rupture, arc flash, fire | Rider, nearby persons |
| **H-11** | HV contactor welding / inability to disconnect | HV always present, fire/shock risk | Rider, emergency responders |
| **H-12** | IGBT shoot-through (HS and LS simultaneously ON) | Immediate DC link short, fire, catastrophic failure | Rider |
| **H-13** | Failure to execute safe state (SSO) on detected fault | Hazardous operation continues despite fault | Rider |
| **H-14** | Corrupted or latched tractive effort command held indefinitely | Persistent unintended acceleration or braking | Rider |
| **H-15** | Incorrect tractive effort command due to software error | Non-requested tractive effort, unexpected drivability | Rider |
| **H-16** | Gate driver fault (DESAT/UVLO/TSD) not acted upon | Phase loss, imbalance, unexpected tractive effort | Rider |
| **H-17** | PWM deadtime violation or stuck-on not detected upstream | Shoot-through, DC link short, fire | Rider |

> **H-03a: Loss of Tractive Effort During Cornering**
>
> This hazard was added after review of motorcycle dynamics. When a motorcycle is at significant lean angle, the rider uses tractive effort to maintain the cornering line. The rear tire is loaded and generating lateral force. A sudden complete loss of tractive effort removes the lateral force component that balances the turn, causing the bike to **stand up and run wide**. Unlike a 4-wheeled vehicle where loss of power merely reduces speed, on a leaned motorcycle this causes an immediate and involuntary change in trajectory. On a racetrack this means leaving the track surface; on a mountain road this means crossing the centerline or leaving the roadway. Recovery requires significant rider skill and sufficient road/runoff area, neither of which may be available. This hazard elevates the controllability of loss-of-tractive-effort events during cornering to **C3**.

# 5. Risk Assessment & ASIL Assignment

### Severity (ISO 26262-3 Table 1)

| Class | Description |
| --- | --- |
| **S1** | Light and moderate injuries |
| **S2** | Severe, life-threatening (survival probable) |
| **S3** | Life-threatening to fatal (survival uncertain) |

### Exposure (ISO 26262-3 Table 2)

| Class | Description |
| --- | --- |
| **E1** | Very low probability (<1% of time) |
| **E2** | Low (1–10% of time) |
| **E3** | Medium (10–50% of time) |
| **E4** | High (>50% of time) |

### Controllability (ISO 26262-3 Table 3)

| Class | Description |
| --- | --- |
| **C1** | Simply controllable |
| **C2** | Normally controllable (requires attention/skill) |
| **C3** | Difficult to control or uncontrollable |

> **Motorcycle Controllability Consideration:** Motorcycles are inherently less stable than 4-wheeled vehicles. Unintended tractive effort at the rear wheel is especially difficult to control due to: (1) only two contact patches, (2) rider must manage roll/pitch/yaw simultaneously, (3) high CG relative to wheelbase, (4) single driven wheel with limited traction reserve. Unintended acceleration/braking at highway speed is therefore **C3** rather than C2. Additionally, loss of tractive effort **during cornering at lean** (OS-10a) is **C3** because the rider has no ability to recover trajectory at full lean.

## ASIL Assignment

**Table 10 — Hazard Risk Assessment & ASIL Assignment**

| Hazard | Worst OS | S | E | C | Target ASIL | Achievable Now | Rationale |
| --- | --- | --- | --- | --- | --- | --- | --- |
| **H-01** | OS-06 (85–150 mph) | S3 | E3 | C3 | D | D | Unintended tractive effort at high speed causes loss of control. E3 reflects highway/track use. Achievable ASIL D via dual-MCU ASIL B(D) decomposition: coprocessor provides independent throttle ADC monitoring, independent CAN snooping, and 1oo2 power kill. |
| **H-02** | OS-01 (stationary) | S2 | E2 | C2 | B | B | Rearward tip-over at standstill. Rider can brace. |
| **H-03** | OS-06 (highway) | S3 | E3 | C2 | C | C | Sudden loss of tractive effort at highway speed. E3 reflects highway/track proportion of total operating time. Rear collision risk. |
| **H-03a** | OS-10a (corner at limit) | S3 | E3 | C3 | C | C | Loss of tractive effort mid-corner causes bike to stand up and run wide. Extremely difficult to recover at full lean. |
| **H-04** | OS-09 (braking) | S2 | E3 | C2 | A | A | Loss of regen; friction brakes remain as backup. |
| **H-05** | OS-13 (wet road) | S3 | E3 | C3 | C | C | Unexpected deceleration on wet surface during cornering. |
| **H-06** | OS-06 (highway WOT) | S3 | E3 | C3 | C | C | Wheel spin at speed. Dual-MCU independent current monitoring detects overcurrent within 100 ms. DESAT handles hard short-circuit (<2 us). |
| **H-07** | OS-14 (desert) | S3 | E2 | C2 | B | B | IGBT thermal runaway. 3x redundant temp sensing. |
| **H-08** | OS-06 (downhill) | S3 | E2 | C3 | C | C | Loss of encoder at speed. Single physical encoder is an external constraint; both MCUs independently monitor the same encoder signal for plausibility. |
| **H-09** | OS-01 (crash/service) | S3 | E1 | C2 | A | A | HV shock. HVIL + reinforced isolation. |
| **H-10** | OS-08 (regen downhill) | S3 | E2 | C2 | B | B | DC link overvoltage from regen. |
| **H-11** | OS-01 (post-crash) | S3 | E1 | C3 | B | B | HV present after crash endangers responders. |
| **H-12** | OS-07 (hard accel) | S3 | E2 | C3 | C | C | Shoot-through. NCV57100 anti-shoot-through is HW non-ASIL. Coprocessor independently monitors all 6 PWM output pairs for deadtime violations and stuck-on/stuck-off. |
| **H-13** | OS-06 (fault at speed) | S3 | E3 | C3 | D | D | Six redundant SSO pathways (Path 1: TIM1_BKIN; Path 2: 1oo2 power kill via GATE_DRIVE_PWR1_ENABLE / GATE_DRIVE_PWR2_ENABLE; Path 3: 3.3 V rail loss; Path 4: GATE_DRIVE_RESET; Path 5: coprocessor watchdog/NRST; Path 6: coprocessor independent fault trigger). |
| **H-14** | OS-06 (highway) | S3 | E3 | C3 | C | C | Stuck tractive effort command. WDT catches CPU halt. |
| **H-15** | OS-06 (highway) | S3 | E3 | C3 | D | D | Software error producing incorrect tractive effort. Coprocessor provides independent verification: cross-checks commanded torque vs. measured currents, monitors CAN torque commands, and can trigger independent SSO via GATE_DRIVE_PWR2_ENABLE. |
| **H-16** | OS-07 (hard accel) | S3 | E2 | C3 | C | C | Gate driver fault not acted upon. Both MCUs independently monitor the OR'd FLT line. Coprocessor additionally monitors the combined READY signal and PWM outputs to detect stuck-active FLT. |
| **H-17** | OS-07 (hard accel) | S3 | E3 | C3 | C | C | PWM fault upstream of gate driver. Coprocessor independently monitors all 6 PWM output pairs (PH_U/V/W_HIGH/LOW) for deadtime violations, stuck-on, and stuck-off. |

# 6. Safety Goals

**Table 11 — Safety Goals**

| SG | Safety Goal | Target | Achievable | Hazards | Safe State |
| --- | --- | --- | --- | --- | --- |
| **SG-01** | Prevent unintended positive tractive effort when rider not requesting propulsion. Unintended tractive effort ≤10 Nm for ≤200 ms before safe state. | D | D | H-01, H-15 | SSO, zero tractive effort |
| **SG-02** | Prevent unintended reverse tractive effort. Reverse rejected → safe state. | B | B | H-02 | SSO, zero tractive effort |
| **SG-03** | Safe degradation on loss of tractive effort. Tractive effort cut-off rate-limited to ≤200 Nm/s. | C | C | H-03, H-03a | Zero tractive effort (gradual) |
| **SG-04** | Ensure regenerative braking availability. Friction brakes remain on loss. | A | A | H-04 | Zero regen (gradual) |
| **SG-05** | Prevent unintended regenerative braking. Uncommanded >10 Nm → safe state within 200 ms. | C | C | H-05 | SSO, zero tractive effort |
| **SG-06** | Limit max tractive effort to calibrated max. Over-torque >110% → safe state within 100 ms. | C | C | H-06 | SSO, zero tractive effort |
| **SG-07** | Detect over-temperature, progressively derate. Critical temp → safe state. | B | B | H-07 | SSO, monitoring |
| **SG-08** | Detect loss of rotor position → safe state within 100 ms. | C | C | H-08 | SSO, zero tractive effort |
| **SG-09** | Maintain HV isolation (>500 Ohm/V). Isolation fault → safe state. | A | A | H-09 | SSO, contactor open |
| **SG-10** | Detect DC link bus overvoltage, limit regen. Critical OV → safe state. | B | B | H-10 | SSO, regen disable |
| **SG-11** | Detect HVIL interruption, request contactor open within 100 ms. | B | B | H-11 | SSO, HV disconnect |
| **SG-12** | Prevent IGBT shoot-through. Risk → PWM disable <10 us via hardware. | C | C | H-12 | SSO, HW PWM kill |
| **SG-13** | Achieve safe state (SSO) within 200 ms of any fault. Safe state entry independent of main control loop. | D | D | H-13, H-14 | SSO, HW-enforced |
| **SG-14** | Detect any NCV57100 fault (DESAT, UVLO, TSD) and transition to safe state. | C | C | H-16 | SSO, FLT detect |
| **SG-15** | Detect PWM deadtime violations and stuck-on faults. Deadtime collapse or stuck-high >100 us → safe state. | C | C | H-17 | SSO, PWM kill |

# 7. Functional Safety Requirements

**Table 12 — Functional Safety Requirements (FSRs)**

| FSR | Requirement | Tgt | Now | SG |
| --- | --- | --- | --- | --- |
| **FSR-01** | Read dual redundant tractive effort control pots on both MCUs. Discrepancy >5% or either OOR detected by either MCU → safe state. | D | D | SG-01 |
| **FSR-02** | Tractive effort command plausibility: commanded current reference vs. measured phase currents. Deviation >20% for >100 ms → safe state. | D | C | SG-01, SG-06 |
| **FSR-03** | Tractive effort control input rate-limited. Max tractive effort rate: 500 Nm/s (calibration parameter). | D | C | SG-01 |
| **FSR-04** | Reject reverse tractive effort when speed >0. Reverse only when stationary AND explicitly selected. | B | B | SG-02 |
| **FSR-05** | On fault, ramp down tractive effort at ≤200 Nm/s before SSO. Abrupt removal prohibited unless <50 ms safety timing requires faster response. | C | C | SG-03 |
| **FSR-06** | Monitor regenerative braking tractive effort continuously. Uncommanded >10 Nm for >200 ms → safe state. | C | C | SG-05 |
| **FSR-07** | Max tractive effort limit: software LUT on both MCUs with cross-check. Dual-MCU independent current monitoring detects overcurrent within 100 ms. DESAT handles hard short-circuit (<2 us). | C | C | SG-06 |
| **FSR-08** | One NTC temp sensor per IGBT module (3 total), 2-out-of-3 voting with 100 °C hard cap. Critical threshold in ECC memory. | B | B | SG-07 |
| **FSR-09** | Encoder loss → safe state within 100 ms. Sensorless fallback if implemented: ≤2 s, ≤30% tractive effort. | C | C | SG-08 |
| **FSR-10** | HVIL monitored continuously. Interruption → immediate PWM disable + BMS contactor open request within 50 ms. | B | B | SG-09, SG-11 |
| **FSR-11** | DC link bus voltage monitored with isolated ADC. OV threshold → immediate regen disable. Critical OV → SSO within 50 ms. | B | B | SG-10 |
| **FSR-12** | NCV57100 complementary inputs (IN+/IN−) prevent HS+LS simultaneous conduction. Power-on self-test confirms. | C | A | SG-12 |
| **FSR-13** | NCV57100 DESAT detection active on all six IGBTs. DESAT event → local PWM disable within <2 us, independent of MCU. | C | A | SG-12, SG-14 |
| **FSR-14** | STM32 breakpoint input → HW PWM disable within <10 us on any critical fault. Independent of main CPU execution. | D | C | SG-13 |
| **FSR-15** | Independent windowed watchdog. Failure to service → automatic PWM disable + system reset. Timeout ≤50 ms. | D | C | SG-13, SG-01 |
| **FSR-16** | Power-on self-test (POST): ADC references, current sensor offsets, gate driver communications, encoder signal, HVIL continuity, watchdog, NCV57100 DESAT self-test. All must pass before PWM enable. | D | C | SG-13 |
| **FSR-17** | IO board CAN heartbeat 1 s timeout. Loss → safe-state defaults (brake pressed, kickstand down) and tractive effort restricted to zero. | C | C | SG-01 |
| **FSR-18** | Tractive effort control limit switch monitored independently of analog pots. Activation overrides any analog value and commands zero tractive effort. | D | C | SG-01 |
| **FSR-19** | Boot-time CRC-32 over safety-critical code and calibration data. Mismatch → PWM enable prevented, fault logged. | D | C | SG-01, SG-13 |
| **FSR-20** | ECC RAM for all safety-critical variables. Single-bit errors corrected (SECDED). Double-bit errors → safe state. | D | C | SG-15 |
| **FSR-21** | Monitor DC link bus undervoltage. UV → tractive effort derate, critical UV → safe state. Prevents overcurrent due to insufficient DC link voltage. | B | B | SG-03, SG-10 |

# 8. Current Design Coverage

**Table 13 — Honest Assessment of Design vs. FSRs**

| FSR | Tgt | Now | Status | Existing / Planned | Gap |
| --- | --- | --- | --- | --- | --- |
| FSR-01 | D | D | Covered | Dual pots on both MCUs; independent voters | Achieved via dual-MCU ASIL B(D) decomposition |
| FSR-02 | D | D | Planned | Commanded vs. measured cross-check on both MCUs | Not yet implemented in firmware (Phase 2) |
| FSR-03 | D | C | Covered | Rate limiter implemented | Part of SG-01 decomposition; main MCU handles rate limiting |
| FSR-04 | B | B | Planned | Reverse interlock: speed >100 rpm → reverse clamped | Not yet implemented in firmware (GAP-SW-02) |
| FSR-05 | C | C | Planned | Direct SSO on fault today | Ramp-down ≤200 Nm/s to be implemented (GAP-HW-02) |
| FSR-06 | C | C | Planned | Uncommanded regen monitor on both MCUs | Not yet implemented in firmware (Phase 2) |
| FSR-07 | C | C | Covered | Dual-MCU independent current monitoring | 100 ms detection sufficient; DESAT for hard shorts |
| FSR-08 | B | B | Planned | Three redundant IGBT temp sensors (hardware) | 2oo3 voter software not yet implemented (Phase 2) |
| FSR-09 | C | C | Limited | Single encoder (external constraint) | Immediate SSO on loss; bounded sensorless if used |
| FSR-10 | B | B | Covered | HVIL circuit implemented | Verify ≤50 ms E2E |
| FSR-11 | B | B | Covered | DC link isolated ADC | Define OV thresholds |
| FSR-12 | C | A | Covered | NCV57100 complementary inputs | Non-ASIL; no credit claimed |
| FSR-13 | C | A | Covered | NCV57100 DESAT on all six | Non-ASIL; no credit claimed |
| FSR-14 | D | D | Covered | Breakpoint input to HW PWM disable + 1oo2 gate drive power kill | Six redundant SSO pathways; either MCU achieves SSO independently |
| FSR-15 | D | C | Covered | Independent watchdog on STM32 | On-chip WDT subject to common cause |
| FSR-16 | D | C | Partial | NCV57100 DESAT self-test exists | Expand to all safety functions |
| FSR-17 | C | C | Planned | 1-second heartbeat timeout confirmed | Safe defaults on CAN loss |
| FSR-18 | D | C | Covered | Limit switch input implemented | Verify override precedence |
| FSR-19 | D | C | To implement | STM32 flash CRC peripheral available | Add boot-time CRC check |
| FSR-20 | D | C | Covered | STM32H723 has ECC RAM | Enable + DED handler to safe state |
| FSR-21 | B | B | Planned | DC link ADC can measure UV | Define UV thresholds and response |

# 9. Gap Analysis

## 9.1 Architecture Gaps

> **GAP-ARCH-01: Single-Core MCU Without Independent Monitor (P0)**
>
> **Issue:** All safety mechanisms execute on one STM32H723. A single MCU fault (clock failure, latch-up, supply collapse) can disable all safety functions simultaneously. The independent watchdog is on-chip and subject to common-cause failure.
>
> **Impact:** SG-01 and SG-13 target ASIL D, achievable via dual-MCU ASIL B(D) + ASIL B(D) decomposition (Table 11). The remaining gap is the formal ASIL D claim, pending the Dependent Failure Analysis (LIMIT-08).
>
> **Mitigation path:** Dual-MCU ASIL B(D) decomposition — implemented. Main MCU + coprocessor each achieve ASIL B(D); combined via 1oo2 voter on safe state actuation.

> **GAP-ARCH-02: Power Kill Without Feedback Monitoring (P1, was P0)**
>
> **Issue:** The GATE_DRIVE_PWR1_ENABLE (main MCU) and GATE_DRIVE_PWR2_ENABLE (coprocessor) paths provide a 1oo2 active SSO mechanism. **Feedback:** GATE_DRIVE_PWR1_FB and GATE_DRIVE_PWR2_FB provide independent per-supply status. When the NCV57100 detects VDD_UVLO (loss of gate drive supply), it asserts FLT (active low), which both MCUs can read. The 1oo2 architecture means a stuck-high GATE_DRIVE_PWR1_ENABLE is not a single-point failure — the coprocessor can still achieve SSO via GATE_DRIVE_PWR2_ENABLE (Path 2b), GATE_DRIVE_RESET (Path 4), or NRST (Path 5). The shared 3.3V rail provides a passive SSO path (NCV57100 pull-down on VDD loss, Path 3).
>
> **Impact:** SG-13 targets ASIL D via ASIL B(D) decomposition. Six SSO pathways exist (see Section 2.2). The 1oo2 power kill with independent feedback provides diagnostic coverage for the supply kill path.
>
> **Mitigation path:** Implemented — GATE_DRIVE_PWR1_FB (main MCU) and GATE_DRIVE_PWR2_FB (coprocessor) provide independent per-supply feedback. Six SSO pathways (TIM1_BKIN; 1oo2 power kill via GATE_DRIVE_PWR1_ENABLE / GATE_DRIVE_PWR2_ENABLE; 3.3 V rail loss; GATE_DRIVE_RESET; coprocessor watchdog/NRST; coprocessor independent fault trigger) provide extensive redundancy. Verify feedback paths in C-17, C-26, C-27, S-10, and S-11.

> **GAP-ARCH-03: Non-ASIL Gate Driver Protections (P1)**
>
> **Issue:** NCV57100 internal protections not ASIL-rated. OR'd FLT output is a single shared wire (single point, not diagnosed), though it is now read by both MCUs.
>
> **Impact:** SG-12, SG-14, SG-15 limited. ASIL credit requires external monitoring.
>
> **Mitigation path:** Coprocessor FLT/READY monitoring and PWM integrity check.

## 9.2 Component Gaps

> **GAP-HW-01: Hardware Overcurrent Comparator — CLOSED (Not Required)**
>
> The schematic includes LM397 comparators for phase and DC link overcurrent detection, but these are **not required for safe operation**. Hard short-circuit protection is handled by NCV57100 DESAT (<2 us). Regular overcurrent (non-DESAT) is detected within **100 ms** by the dual-MCU integrated monitoring: both the main STM32H723 and the coprocessor STM32G474 independently sample all four current sense channels at high rate. Either MCU detecting overcurrent triggers SSO via its independent gate drive power kill path. This 100 ms detection time is faster than the thermal time constant of the IGBT module and therefore cannot cause permanent damage. The LM397 comparators are retained on the PCB as a redundant monitoring layer but are not relied upon for the safety case. **Closed — integrated dual-MCU monitoring is sufficient.**

> **GAP-HW-02: No Tractive Effort Ramp-Down (P1)**
>
> Direct SSO on fault. Implement software ramp at ≤200 Nm/s before SSO.

> **GAP-SW-01: No Boot CRC (P1)**
>
> FSR-19: Boot CRC verification is not yet implemented. Add a boot-time CRC-32 using the STM32 CRC peripheral to validate safety-critical code and calibration data before PWM enable.

> **GAP-SW-02: No Reverse Interlock (P1)**
>
> Software interlock: speed >100 rpm forward → negative tractive effort clamped to zero.

> **GAP-SW-03: Sensorless Fallback Policy Undefined (P1)**
>
> Define: immediate SSO on encoder loss, or bounded sensorless (≤2 s, ≤30% tractive effort).

## 9.2 Gap Summary

**Table 14 — Gap Mitigation Priority**

| Gap | Priority | Mitigation | Effort |
| --- | --- | --- | --- |
| Dual MCU with coprocessor | **RESOLVED** | STM32G474RCTx implemented — 1oo2 power kill, independent ADC, challenge/response watchdog, independent CAN snoop | Complete |
| HW overcurrent comparator | **CLOSED** | Dual-MCU integrated monitoring detects overcurrent within 100 ms — sufficient for SSO without damage. LM397 retained as non-safety redundant layer. | N/A |
| Power kill feedback monitoring | **P1** | GATE_DRIVE_PWR1_FB and GATE_DRIVE_PWR2_FB provide independent per-supply feedback. Verify in S-16 through S-19. | Low |
| Non-ASIL gate driver credit | **P1** | Coprocessor monitors FLT, READY, all 6 PWM outputs. Verify cross-check logic in C-14, S-16. | Medium |
| Tractive effort ramp-down | **P1** | Software | Medium |
| Boot CRC | **P1** | STM32 CRC peripheral | Low |
| Reverse interlock | **P1** | Software | Low |
| Sensorless policy | **P1** | Software + documentation | Low |
| No DFA (ISO 26262-9) | **P0** | Dependent Failure Analysis | Medium |
| No EMI/EMC pre-compliance | **P1** | CISPR 25 pre-compliance test | Medium |

**Note:** With the dual-MCU architecture (STM32H723 + STM32G474 coprocessor), ASIL D is achievable for SG-01 and SG-13 via ASIL B(D) decomposition. The four hazards previously limited to ASIL A (H-06, H-16, H-17, and H-13 safe state failure) are now fully covered: H-06 by dual-MCU independent current monitoring (100 ms detection + independent power kill), H-16 by coprocessor FLT/READY/PWM monitoring, H-17 by coprocessor independent deadtime monitoring, and H-13 by six redundant SSO pathways. GAP-HW-01 (HW OCP comparator) is closed — dual-MCU integrated monitoring is sufficient. No hazards remain below their target ASIL.

# 10. Fault Injection Test Plan

## 10.1 Test Philosophy

The purpose of this Fault Injection Test Plan is to provide a comprehensive, traceable methodology for verifying that the safety mechanisms implemented in the traction inverter/VCU detect faults and transition to the defined safe state (SSO) within the required time budgets. The test plan defines **99 tests** organized into four categories:

- **Component-Level Tests (C-01 to C-50):** Individual hardware component validation — inject faults into a single sensor, input, or protection circuit and verify detection and safe state entry. Includes supply faults (brownout, rail shorts), phase faults (open, short-to-short, short-to-DC), gate driver validation, clock failures, GPIO stuck-at, ADC drift, SPI, CAN bus off, watchdog starvation, thermal runaway, pre-charge, Miller clamp, propagation delay, deadtime, isolation.
- **System-Level Tests (S-01 to S-19):** Full-system fault scenarios — simulate real-world fault conditions with the complete hardware and software running closed-loop control and verify end-to-end response. Includes thermal camera survey, full-load regen, startup/shutdown sequencing, and power dip ride-through.
- **Integration-Level Tests (I-01 to I-18):** External interface and communication fault scenarios — simulate failures in connected systems (BMS, IO board, CAN bus) and verify safe degradation. Includes CAN fuzzing, bus load testing, multi-node faults, and wiring faults.
- **Environmental Tests (E-01 to E-12):** Environmental and stress validation — vibration, thermal shock, humidity, EMI immunity, ESD, and water ingress. These are type tests for production qualification.

Each test case is designed with the following principles:

1. **Every Safety Goal (SG-01 through SG-15) is covered by at least one test case.**
2. **Every Functional Safety Requirement (FSR-01 through FSR-21) is covered by at least one test case.**
3. **Every identified hazard (H-01 through H-17, including H-03a) is covered by at least one test case.**
4. **Response time requirements are verified** where measurable (e.g., <10 us HW PWM disable, ≤50 ms WDT timeout, ≤200 ms safe state entry).
5. **Fault injection is realistic** — faults represent credible failure modes observed in traction power electronics systems (stuck sensor, shorted wiring, open connection, corrupted data, supply anomaly, etc.).
6. **Tests are independently executable** where possible to allow incremental validation as software matures.

## 10.2 Test Environment

**Table 15 — Test Equipment and Setup**

| Item | Description | Purpose |
| --- | --- | --- |
| **Test article** | Traction inverter/VCU PCB with STM32H723, six NCV57100, IGBT power stage, all sensors | Device under test (DUT) |
| **DC link source** | Programmable DC power supply 0–400 V, ≥50 A or traction battery with contactor control | DC link for low-power testing; traction battery for high-power |
| **Test motor** | Low-voltage PMSM on dyno or equivalent inertia load | Closed-loop traction motor control testing |
| **Dyno** | Motor dynamometer with torque/speed measurement and programmable load | Full-load characterization and fault testing under load |
| **Fault injection hardware** | Switchable resistor networks, relay cards, programmable loads, short-circuit switches | Physically inject faults into sensors, wiring, gate signals |
| **Oscilloscope** | 4-channel, ≥100 MHz, with current probes and HV differential probes | Timing verification of PWM disable, DESAT response, gate signals |
| **CAN interface** | PCAN or equivalent USB-CAN adapter | Monitor CAN traffic, simulate BMS/IO board messages |
| **RTE (Real Time Examiner)** | Host tool connected via CAN or debug interface | Monitor internal variables, tractive effort commands, fault status |
| **Thermal chamber** | Environmental chamber −20 °C to +60 °C | Overtemperature derating and thermal fault testing |
| **Isolation tester** | Megohmmeter / insulation resistance tester | HV isolation measurement |
| **Thermal camera** | Infrared camera, 320x240 minimum, <50 mK NETD | Full-load thermal survey (S-06), hotspot identification |
| **Vibration table** | Random/sinusoidal vibration table, 5–2000 Hz, ≥10 g | Environmental vibration testing (E-01 to E-03) |
| **EMC chamber** | ALSE or anechoic chamber with RF amplifiers | Radiated EMI immunity (E-08), conducted BCI (E-09) |
| **ESD gun** | ESD simulator per ISO 10605 | Contact and air discharge testing (E-10, E-11) |
| **Humidity chamber** | Environmental chamber with humidity control | High humidity operation (E-07) |
| **Water spray rig** | IPX4/IPX5/IPX6 spray nozzles per IEC 60529 | Water ingress validation (E-12) |
| **Common-mode probe** | High-bandwidth differential probe, ≥100 MHz | Bearing voltage / common-mode measurement (C-36) |
| **Shaft voltage probe** | Carbon brush or capacitive coupling to motor shaft | Bearing voltage ratio measurement (C-36) |

### Safety During Testing

All high-power fault injection tests (tests involving >48 V DC link or >10 A phase current) must be conducted with:

- Personnel trained in HV electrical safety (NFPA 70E or equivalent)
- Appropriate PPE (HV-rated gloves, face shield, non-conductive tools)
- Emergency stop button within arm's reach of all test stations
- Fire extinguisher (Class C) present
- Test area marked and restricted to authorized personnel only
- Pre-charge/discharge protocol followed for all DC link connections

## 10.3 Component-Level Tests

#### C-01: Tractive Effort Control Potentiometer 1 Open Circuit

**Objective:** Verify FSR-01 — Dual tractive effort control plausibility detects open circuit on primary potentiometer.

**Covered:** SG-01 (ASIL D), FSR-01, H-01

**Procedure:**

1. Connect tractive effort control assembly with both pots functional. System at key-on, traction motor not running.
2. Apply 50% tractive effort request via both pots (matched within 2%).
3. While maintaining input, open-circuit potentiometer 1 signal wire (disconnect from PCB).
4. Observe system response via RTE and oscilloscope on PWM outputs.
5. Repeat with traction motor spinning at 50% rated speed under dyno load.

**Pass Criteria:** System detects pot 1 out-of-range/discrepancy >5% within <100 ms and transitions to safe state (SSO). No tractive effort produced after fault. Fault logged with correct code.

**Fail Criteria:** Tractive effort continues after fault, fault detection >200 ms, or incorrect fault code logged.

**Why this covers the requirement:** An open circuit on one potentiometer is a credible wiring failure. The dual-pot plausibility check (FSR-01) must detect the discrepancy and enter safe state before any unintended tractive effort can be commanded. This directly addresses H-01 (unintended tractive effort) by ensuring a single sensor fault cannot cause an incorrect tractive effort request.

#### C-02: Tractive Effort Control Potentiometer 2 Shorted to +5 V

**Objective:** Verify FSR-01 — Dual tractive effort control plausibility detects short-to-rail on secondary potentiometer.

**Covered:** SG-01 (ASIL D), FSR-01, H-01

**Procedure:**

1. Connect tractive effort control with both pots functional. System at key-on.
2. Apply 25% tractive effort request via pot 1, short pot 2 to +5 V reference (simulating wiring short).
3. Observe response via RTE and scope.
4. Repeat with traction motor running at 25% speed under load.

**Pass Criteria:** Discrepancy >5% detected within <100 ms. Safe state entered. Fault logged.

**Why this covers the requirement:** Short-to-rail is a common wiring fault (chafed harness). Pot 2 at 100% while pot 1 at 25% creates a 75% discrepancy that must be caught. This is a distinct failure mode from C-01 (open circuit) and verifies the plausibility check works for both high and low anomalies.

#### C-03: Tractive Effort Control Potentiometer 1 Shorted to Ground

**Objective:** Verify FSR-01 — Short-to-ground detection on primary pot.

**Covered:** SG-01 (ASIL D), FSR-01, H-01

**Procedure:** Short pot 1 signal to ground while pot 2 at 50%. Observe response. Repeat under motor load.

**Pass Criteria:** Discrepancy detected <100 ms. Safe state. Fault logged.

**Why this covers the requirement:** Ground short is distinct from open and +5 V short (different ADC reading). Verifies the plausibility check is robust to all three common wiring fault modes.

#### C-04: Tractive Effort Control Potentiometer Drift (Gradual Divergence)

**Objective:** Verify FSR-01 detects gradual potentiometer mismatch (simulating sensor degradation).

**Covered:** SG-01 (ASIL D), FSR-01, H-01

**Procedure:** Use programmable resistors to gradually increase offset between pot 1 and pot 2 from 0% to 10% over 10 seconds while traction motor running at constant speed. Observe when fault triggers.

**Pass Criteria:** Fault triggers when discrepancy exceeds 5% threshold. No false trips below threshold.

**Why this covers the requirement:** Gradual sensor drift (wear, temperature aging) must be distinguished from normal variation. Verifies threshold is calibrated correctly and not overly sensitive (nuisance trips) or insensitive (missed faults).

#### C-05: Tractive Effort Control Limit Switch Activation at Speed

**Objective:** Verify FSR-18 — Limit switch independently commands zero tractive effort, overriding analog values.

**Covered:** SG-01 (ASIL D), FSR-18, H-01

**Procedure:**

1. Traction motor running at 50% rated speed under dyno load with 50% tractive effort request applied.
2. Activate tractive effort control limit switch (mechanical end-stop) while maintaining potentiometer position.
3. Observe tractive effort command and PWM output via RTE and scope.

**Pass Criteria:** Tractive effort command drops to zero within <50 ms of limit switch activation, regardless of potentiometer position. PWM disabled or zero duty. Fault logged.

**Why this covers the requirement:** The limit switch is an independent hardware path to zero tractive effort. Even if both potentiometers fail or software miscalculates, the mechanical switch must unconditionally override. This is a last-resort protection against a stuck control cable or sensor malfunction.

#### C-06: Phase Current Sensor Offset Drift

**Objective:** Verify FSR-02 — Tractive effort command plausibility detects current sensor offset error.

**Covered:** SG-01 (ASIL D), SG-06 (ASIL C), FSR-02, H-01, H-06

**Procedure:**

1. Traction motor running at 50% rated tractive effort under dyno load.
2. Inject DC offset (+20% of rated current) into one phase current sensor via external bias circuit.
3. Observe commanded tractive effort vs. measured current. Note when plausibility check triggers.
4. Repeat with −20% offset.

**Pass Criteria:** Plausibility deviation >20% detected within <100 ms. Safe state entered.

**Why this covers the requirement:** Current sensor offset drift (hall sensor temperature drift, ADC reference drift) causes the MCU to misread actual current. FSR-02 requires comparing commanded current reference against measured phase currents. This test verifies the end-to-end plausibility chain catches sensor errors that would lead to incorrect tractive effort.

#### C-07: DC Link Current Sensor Open Circuit

**Objective:** Verify FSR-02 — Loss of DC link current sensor detection.

**Covered:** SG-01 (ASIL D), FSR-02, FSR-07, H-01, H-06

**Procedure:** Open-circuit DC link current sensor signal wire while traction motor running at 25% load. Observe plausibility check and safe state entry.

**Pass Criteria:** Sensor out-of-range detected. If sensor has reference signal: reference mismatch detected. Safe state within <200 ms.

**Why this covers the requirement:** The DC link sensor provides the sum current check against phase currents. Its loss removes a plausibility layer. This verifies the system handles the failure gracefully.

#### C-08: IGBT Overtemperature (Simulated)

**Objective:** Verify FSR-08 — 2-out-of-3 temperature voting and progressive derating.

**Covered:** SG-07 (ASIL B), FSR-08, H-07

**Procedure:**

1. Use programmable resistor or heat gun to elevate one IGBT temp sensor reading above warning threshold (e.g., 100 °C) while other two sensors remain normal.
2. Observe tractive effort derating behavior via RTE.
3. Elevate second sensor to warning threshold. Verify further derating.
4. Elevate all three sensors above critical threshold (e.g., 130 °C). Verify safe state entry.
5. Test 1-of-3 failure: one sensor stuck at 150 °C (implausible) while others normal. Verify 2oo3 voter rejects outlier and continues operation.

**Pass Criteria:** Progressive derate at warning thresholds. Safe state at critical threshold. 2oo3 voter correctly rejects single implausible sensor. Response times: derate <500 ms, safe state <1 s from critical threshold crossing.

**Why this covers the requirement:** Thermal runaway of IGBTs is a credible failure mode under high load or blocked cooling. FSR-08 requires 2-out-of-3 voting to tolerate one sensor failure. This test verifies both the temperature response curve and the voting logic.

#### C-09: Traction Motor Encoder Signal Loss (Quadrature)

**Objective:** Verify FSR-09 — Encoder loss detection and safe state entry.

**Covered:** SG-08 (ASIL C), FSR-09, H-08

**Procedure:**

1. Traction motor spinning at 50% rated speed under dyno load (closed-loop FOC).
2. Disconnect encoder A or B channel (open circuit).
3. Observe FOC behavior and safe state entry via RTE and scope.
4. Repeat with encoder supply disconnected (both channels lost).
5. If sensorless fallback implemented: verify bounded operation (≤2 s, ≤30% tractive effort) before SSO.

**Pass Criteria:** Encoder loss detected within <100 ms. Safe state entered. If sensorless fallback: bounded to ≤2 s and ≤30% tractive effort, then SSO. Fault logged with correct code.

**Why this covers the requirement:** Loss of position feedback at speed causes FOC to lose synchronization, producing unpredictable tractive effort. This is H-08. FSR-09 requires immediate safe state because the single encoder has no redundancy. The test verifies the timeout and response.

#### C-10: DC Link Bus Overvoltage (Simulated)

**Objective:** Verify FSR-11 — DC link bus overvoltage detection and regen disable.

**Covered:** SG-10 (ASIL B), FSR-11, H-10

**Procedure:**

1. System running with DC link at nominal voltage. Traction motor spinning with regen active.
2. Gradually increase DC link bus voltage via programmable supply to overvoltage warning threshold.
3. Verify regen tractive effort is disabled / reduced.
4. Continue increasing to critical overvoltage threshold.
5. Verify safe state entry (SSO) within <50 ms of critical threshold crossing.

**Pass Criteria:** Regen disabled at warning threshold. SSO at critical threshold within <50 ms. No overvoltage damage to DC link capacitors or semiconductors.

**Why this covers the requirement:** Overvoltage occurs during hard regen on long downhills or when BMS cell balancing fails. FSR-11 requires two thresholds: a warning that disables regen (preventing voltage rise) and a critical that forces SSO (preventing capacitor rupture). Both must be verified.

#### C-11: DC Link Bus Undervoltage (Simulated)

**Objective:** Verify FSR-21 — DC link bus undervoltage detection and response.

**Covered:** SG-03 (ASIL C), SG-10 (ASIL B), FSR-21, H-03, H-03a

**Procedure:** Gradually reduce DC link bus voltage while traction motor running at 50% load. Observe tractive effort derating and safe state entry at critical UV threshold.

**Pass Criteria:** Tractive effort derated progressively as bus voltage drops. Safe state at critical UV. No overcurrent event due to insufficient DC link voltage.

**Why this covers the requirement:** Insufficient DC link voltage causes the current controller to saturate, potentially commanding excessive duty cycle that leads to overcurrent when voltage recovers. FSR-21 prevents this by derating and entering safe state before saturation occurs.

#### C-12: HVIL Interruption

**Objective:** Verify FSR-10 — HVIL loop interruption detection and response.

**Covered:** SG-09 (ASIL A), SG-11 (ASIL B), FSR-10, H-09, H-11

**Procedure:**

1. System at key-on with HVIL loop intact.
2. Open HVIL loop (disconnect HVIL connector or cut loop wire).
3. Measure time from HVIL open to PWM disable and contactor open request on CAN1.

**Pass Criteria:** PWM disabled within <50 ms. Contactor open request transmitted on CAN1 within <100 ms. Fault logged.

**Why this covers the requirement:** HVIL interruption indicates a connector disconnection or safety interlock trigger (e.g., crash sensor). FSR-10 requires immediate PWM disable and BMS notification. Both the local response (PWM) and external notification (CAN) must be verified.

#### C-13: Watchdog Timeout (STM32 Failure to Service)

**Objective:** Verify FSR-15 — Independent watchdog detects MCU runaway and forces reset.

**Covered:** SG-13 (ASIL D), SG-01 (ASIL D), FSR-15, H-13, H-14

**Procedure:**

1. Traction motor running at 50% load. System operating normally.
2. Via debug interface, halt CPU execution (simulating code runaway or infinite loop).
3. Measure time from CPU halt to PWM disable and system reset.
4. Alternatively, use a firmware build that intentionally stops servicing the watchdog after a trigger condition.

**Pass Criteria:** PWM disabled and system reset occurs within watchdog timeout period (≤50 ms). No tractive effort produced during or after reset until POST completes and key cycle occurs.

**Why this covers the requirement:** A CPU runaway (e.g., pointer corruption causing infinite loop, stack overflow) can leave PWM running at a constant duty cycle, producing H-14 (latched tractive effort). FSR-15 requires the watchdog to catch this and force safe state. The halt-via-debug method is a realistic fault injection technique for this failure mode.

#### C-14: STM32 Breakpoint Input (HW PWM Disable)

**Objective:** Verify FSR-14 — Hardware breakpoint input disables all PWM within <10 us, independent of software.

**Covered:** SG-13 (ASIL D), FSR-14, H-13

**Procedure:**

1. Traction motor running at 50% load with active PWM.
2. Assert breakpoint input pin (pull to active level via external switch).
3. Measure time from breakpoint assertion to all six PWM outputs going low, using oscilloscope.
4. Verify PWM remains disabled while breakpoint held.
5. De-assert breakpoint and verify PWM does NOT resume without system reset and POST.

**Pass Criteria:** All PWM outputs disabled within <10 us of breakpoint assertion. Independent of CPU state (test with CPU halted via debugger to prove hardware-only path). PWM does not auto-resume.

**Why this covers the requirement:** The breakpoint input is the fastest and most reliable safe-state path. It must work even if the CPU is completely non-functional. Measuring with the CPU halted proves the path is truly hardware-independent. The <10 us requirement addresses shoot-through and other time-critical faults.

#### C-15: Gate Driver DESAT (Simulated Short Circuit)

**Objective:** Verify FSR-13 — NCV57100 DESAT protection detects and responds to simulated short circuit.

**Covered:** SG-12 (ASIL C), SG-14 (ASIL C), FSR-13, H-12, H-16

**Procedure:**

1. System at key-on with DC link present. Gate drivers active.
2. Use a low-inductance pulse generator to inject a brief overcurrent pulse into one phase, or use a DESAT test fixture that momentarily pulls the DESAT pin above threshold.
3. Measure FLT output response time and PWM disable via scope.
4. Verify soft turn-off slope (not hard shutoff that causes voltage spike).
5. Verify FLT is latched and readable by STM32.

**Pass Criteria:** DESAT detected and PWM disabled within <2 us. Soft turn-off observed. FLT asserted and latched. System enters safe state.

**Why this covers the requirement:** DESAT is the primary short-circuit protection in the gate driver. While the NCV57100 is non-ASIL, verifying its response time and behavior is essential for the overall safety argument. This test also forms the basis for the DESAT self-test (C-16).

#### C-16: Gate Driver DESAT Self-Test at Power-On

**Objective:** Verify FSR-16 — DESAT self-test confirms protection circuit functional before PWM enable.

**Covered:** SG-12 (ASIL C), SG-14 (ASIL C), FSR-16, H-12, H-16

**Procedure:** Power cycle the system. Observe the DESAT self-test sequence (brief test pulse to verify DESAT detection circuit responds). Verify PWM is not enabled until all six gate drivers pass self-test. Introduce a fault in one DESAT circuit (e.g., open DESAT diode) and verify system refuses to enable PWM.

**Pass Criteria:** Self-test completes on all six channels. PWM enable gated by self-test pass. With DESAT fault injected: system remains in safe state, fault logged, PWM never enabled.

**Why this covers the requirement:** A failed DESAT circuit (open DESAT diode, broken connection) is a latent fault that would prevent short-circuit detection. The power-on self-test must catch this before the system can operate. This is essential for latent fault coverage.

#### C-17: Gate Driver UVLO (Simulated Low Supply)

**Objective:** Verify gate driver UVLO prevents operation at insufficient gate drive voltage.

**Covered:** SG-12 (ASIL C), FSR-12, H-12

**Procedure:** Gradually reduce the +15 V gate driver supply voltage while system is operating. Observe the point at which UVLO triggers and PWM is disabled.

**Pass Criteria:** UVLO triggers at V<sub>UVLO−</sub> ≈ 11.3 V. All PWM disabled. FLT asserted. Safe state entered.

**Why this covers the requirement:** Insufficient gate drive voltage causes the IGBT to operate in the linear region, leading to excessive conduction loss and thermal destruction. UVLO prevents this by forcing gate OFF when supply is inadequate.

#### C-18: ECC RAM Single-Bit Error Injection

**Objective:** Verify FSR-20 — ECC RAM corrects single-bit errors and allows continued operation.

**Covered:** SG-13 (ASIL D), SG-15 (ASIL C), FSR-20

**Procedure:** Use STM32's built-in ECC error injection capability (if available) or debug interface to flip a single bit in a safety-critical RAM location (e.g., tractive effort limit variable). Observe system behavior via RTE.

**Pass Criteria:** Single-bit error detected and corrected. System continues operating. Error logged. No unsafe state entered.

**Why this covers the requirement:** Cosmic radiation and EMI can corrupt RAM. SECDED ECC must handle single-bit errors transparently. This verifies the error correction path works without causing false trips.

#### C-19: ECC RAM Double-Bit Error Injection

**Objective:** Verify FSR-20 — Double-bit errors trigger safe state (uncorrectable).

**Covered:** SG-13 (ASIL D), FSR-20

**Procedure:** Flip two bits in a safety-critical RAM location. Observe system response.

**Pass Criteria:** Double-bit error detected. Safe state entered immediately. Fatal error logged. System requires reset.

**Why this covers the requirement:** Double-bit errors are uncorrectable. The system must treat them as critical faults and enter safe state, because the corrupted data could be a safety-critical variable (tractive effort limit, fault threshold, etc.).

#### C-20: Boot CRC Mismatch

**Objective:** Verify FSR-19 — Boot CRC prevents operation with corrupted firmware.

**Covered:** SG-01 (ASIL D), SG-13 (ASIL D), FSR-19, H-15

**Procedure:** Deliberately corrupt one byte in the safety-critical code flash region (using debugger or flash programming tool). Power cycle and observe boot behavior.

**Pass Criteria:** CRC-32 mismatch detected at boot. PWM enable prevented. Fault logged. System remains in safe state.

**Why this covers the requirement:** Corrupted firmware (flash wear, programming error, EMI during write) could modify the tractive effort mapping, safety thresholds, or fault handling logic. FSR-19 prevents the system from operating with untrusted code. This directly addresses H-15 (incorrect tractive effort from software error).

#### C-21: STM32 Supply Brownout — Gradual Vdd Drop

**Objective:** Verify brownout detection (BOR) triggers safe state before MCU operation becomes unreliable.

**Covered:** SG-13 (ASIL D), FSR-14, FSR-15, H-13, H-14

**Procedure:**

1. System operating at 50% tractive effort. STM32 Vdd = 3.3 V nominal.
2. Using programmable power supply, ramp Vdd down gradually at 10 mV/s.
3. Observe BOR threshold (typical 2.7 V for level 2). Verify reset/assertion before Vdd reaches 2.5 V.
4. Verify PWM disabled at BOR assertion. Verify safe state entered.
5. Repeat with faster ramp (100 mV/s) and slower ramp (1 mV/s).
6. Repeat at different operating points: idle, full load, regen.

**Pass Criteria:** BOR asserts at consistent threshold (±50 mV). PWM disabled before Vdd < 2.5 V. No erratic PWM behavior observed during decay. Safe state entered reliably. Tractive effort zero before MCU becomes unreliable.

**Why this covers the requirement:** A declining supply (failing DC/DC, loose connection, corroded terminal) can cause the MCU to operate in an undefined region where it may output random PWM patterns. The BOR must catch this before erratic behavior occurs. This test validates the brownout threshold and response across multiple slew rates.

#### C-22: Brownout Recovery — Power Dip Ride-Through

**Objective:** Verify system behavior during brief power dips and recovery.

**Covered:** SG-13 (ASIL D), FSR-16, H-13

**Procedure:**

1. System operating at 50% tractive effort.
2. Apply brief Vdd dip: 3.3 V → 2.8 V for 10 ms (above BOR threshold). Verify system continues operating.
3. Apply brief Vdd dip: 3.3 V → 2.6 V for 5 ms (below BOR threshold). Verify BOR reset triggers.
4. Apply Vdd interruption: 3.3 V → 0 V for 100 ms → 3.3 V. Verify full POST sequence executes on recovery.
5. Repeat with dip on +12 V rail (gate drive supply) while monitoring gate driver UVLO.

**Pass Criteria:** Dips above BOR: uninterrupted operation. Dips below BOR: clean reset, PWM disabled, POST runs on recovery. +12 V dip: NCV57100 UVLO triggers, PWM disabled. All cases: safe state maintained during anomaly.

**Why this covers the requirement:** Real-world power disturbances (load dump, starter motor engagement, loose battery connection) cause brief voltage dips. The system must either ride through cleanly or reset cleanly — never enter an undefined operational state. This validates both the brownout detector and the gate driver UVLO independence.

#### C-23: +12 V Logic Rail Short to Ground

**Objective:** Verify system response when +12 V logic rail is shorted to ground.

**Covered:** SG-13 (ASIL D), FSR-14, FSR-15, H-13

**Procedure:**

1. System operating at 50% tractive effort. +12 V rail monitored.
2. Apply controlled short to ground on +12 V rail via current-limited test fixture (<5 A limit to protect wiring).
3. Observe: STM32 supply (3.3 V via RD7-12S033R DC/DC converter), gate driver supplies (via isolated DC/DC), CAN transceivers.
4. Verify which subsystems lose power and which remain operational.
5. Remove short. Verify recovery behavior.

**Pass Criteria:** If +12 V collapse causes 3.3 V dropout: BOR triggers, safe state entered. Gate driver isolated supplies must remain operational (isolated from logic rail) or UVLO triggers safe state. No erratic PWM during collapse. Short removal requires key cycle to resume.

**Why this covers the requirement:** A +12 V short can occur from chafed wiring, failed downstream load, or moisture ingress. The test validates that the system fails safely — either the brownout catches it, or the gate driver UVLO catches it, or both. The isolated gate drive supplies should remain up (they have their own isolated DC/DC), but if they droop, the NCV57100 UVLO provides independent protection.

#### C-24: +5 V Sensor Rail Short to Ground

**Objective:** Verify system response when +5 V sensor supply is shorted.

**Covered:** SG-01 (ASIL D), SG-13 (ASIL D), FSR-01, FSR-09, H-01, H-08

**Procedure:**

1. System operating. +5 V rail powers throttle pots, current sensor references, encoder.
2. Apply controlled short on +5 V rail (current-limited to <2 A).
3. Observe throttle pot readings (should go to zero / OOR). Observe current sensor reference loss.
4. Verify FSR-01 catches pot OOR. Verify FSR-09 catches encoder signal loss (if encoder powered from +5 V).
5. Verify safe state entered before any incorrect tractive effort is produced.

**Pass Criteria:** +5 V short detected via sensor OOR. Safe state entered within <200 ms. No tractive effort produced based on invalid sensor data. Fault logged with distinct DTC.

**Why this covers the requirement:** The +5 V rail powers critical sensors. A short causes all sensors to read zero or OOR. The system must recognize this as a fault condition, not interpret zero readings as valid "zero throttle" and "zero current." This tests the sensor OOR detection paths.

#### C-25: +3.3 V MCU Supply Short to Ground

**Objective:** Verify safe state behavior on MCU supply collapse.

**Covered:** SG-13 (ASIL D), FSR-14, FSR-15, H-13

**Procedure:**

1. System operating at 50% tractive effort. 3.3 V rail monitored.
2. Apply controlled short on 3.3 V rail (current-limited).
3. Observe BOR behavior. If BOR does not trigger (short faster than BOR response), observe gate driver behavior independently.
4. Verify breakpoint PWM disable triggers as supply collapses.
5. Verify watchdog triggers if CPU stalls.

**Pass Criteria:** At least one safe-state path triggers (BOR, watchdog, or breakpoint). PWM disabled. No sustained erratic operation. System requires key cycle after short removal.

**Why this covers the requirement:** The 3.3 V short is the worst-case supply fault because it directly affects the MCU. Multiple independent safe-state paths must exist. This test validates that at least one path works even under the most severe supply fault.

#### C-26: Gate Driver +15 V Supply Short

**Objective:** Verify NCV57100 UVLO response when +15 V gate drive supply is shorted.

**Covered:** SG-12 (ASIL C), SG-14 (ASIL C), FSR-12, FSR-13, H-12, H-16

**Procedure:**

1. System operating. Individual gate driver +15 V supplies monitored (six isolated supplies).
2. Short +15 V supply of one gate driver (U-phase high-side) to ground via current-limited fixture.
3. Observe NCV57100 UVLO trigger. Measure UVLO threshold (typical V<sub>UVLO−</sub> = 11.3 V).
4. Verify FLT asserted. Verify PWM disabled for that phase.
5. Verify system enters safe state (not just disabling one phase, which creates imbalance).
6. Repeat for all six gate driver positions.

**Pass Criteria:** UVLO triggers at V<sub>UVLO−</sub> ≈ 11.3 V (±0.5 V). FLT asserted. PWM disabled. Safe state entered. All six positions show consistent behavior. No IGBT damage.

**Why this covers the requirement:** Each gate driver has its own isolated +15 V supply. A short in one supply (failed DC/DC, broken wire) causes that driver's UVLO to trigger. The FLT output signals the fault. The system must enter safe state because operating with one disabled phase creates severe current imbalance and vibration.

#### C-27: Gate Driver −9 V Supply Short

**Objective:** Verify NCV57100 response when negative gate drive supply is shorted.

**Covered:** SG-12 (ASIL C), FSR-12, H-12

**Procedure:** Same as C-26 but short −9 V supply to ground for each gate driver position.

**Pass Criteria:** Negative supply loss detected. Gate cannot be properly turned off (Miller current has no return path). FLT asserted. Safe state entered. No shoot-through.

**Why this covers the requirement:** The −9 V supply ensures the IGBT stays off during dv/dt transitions (active Miller clamp). Loss of negative rail means the Miller current charges the gate capacitance through the Miller capacitor, potentially turning the IGBT on unexpectedly. The NCV57100 must detect this condition and force safe state.

#### C-28: Phase U Open Circuit (Motor Disconnect)

**Objective:** Verify system detects and responds to an open-circuited motor phase.

**Covered:** SG-08 (ASIL C), FSR-09, H-08

**Procedure:**

1. System operating at 30% tractive effort on dyno. Closed-loop FOC active.
2. Open Phase U connection (using contactor or relay in series with motor lead).
3. Observe current sensor readings: Phase U current drops to zero while V and W show imbalance.
4. Verify FSR-02 (tractive effort plausibility) detects commanded vs. actual current mismatch.
5. Verify safe state entered within <200 ms.
6. Repeat at multiple operating points: low speed, high speed, regen.

**Pass Criteria:** Open phase detected via current imbalance. Safe state entered within <200 ms. No sustained single-phase operation (which would cause severe vibration and potential motor damage). Fault logged with specific DTC.

**Why this covers the requirement:** An open phase (loose terminal, broken wire, burnt connector) causes the motor to operate single-phase. FOC cannot maintain control with one phase open — the current feedback is missing for that phase. The plausibility check (FSR-02) must catch the mismatch between commanded and actual current. Operating single-phase causes severe torque ripple, vibration, and potential bearing damage.

#### C-29: Phase V Open Circuit

**Objective:** Same as C-28 for Phase V.

**Covered:** SG-08 (ASIL C), FSR-09, H-08

**Procedure:** Identical to C-28, open Phase V instead of Phase U.

**Pass Criteria:** Same as C-28.

**Why this covers the requirement:** All three phases must be independently validated. The detection mechanism may behave differently depending on which phase is open due to FOC coordinate transformation dependencies.

#### C-30: Phase W Open Circuit

**Objective:** Same as C-28 for Phase W.

**Covered:** SG-08 (ASIL C), FSR-09, H-08

**Procedure:** Identical to C-28, open Phase W instead of Phase U.

**Pass Criteria:** Same as C-28.

**Why this covers the requirement:** All three phases must be independently validated. The detection mechanism may behave differently depending on which phase is open due to FOC coordinate transformation dependencies.

#### C-31: Phase-to-Phase Short (U-V)

**Objective:** Verify DESAT and overcurrent protection respond to phase-to-phase short circuit.

**Covered:** SG-12 (ASIL C), SG-14 (ASIL C), FSR-13, H-12, H-16

**Procedure:**

1. System at key-on with DC link energized. Low-voltage test recommended (reduced DC link to 50 V) to limit fault energy.
2. Apply phase-to-phase short between U and V using low-inductance contactor.
3. Measure DESAT response time (<2 us target). Measure peak current.
4. Verify soft turn-off (not hard shutoff). Verify no IGBT damage.
5. Verify FLT latched. Verify system safe state.
6. Inspect IGBTs after test (no degradation).

**Pass Criteria:** DESAT triggers within <2 us. Peak current limited by DC link inductance. Soft turn-off observed. No IGBT damage. FLT latched. Safe state entered. System requires reset.

**Why this covers the requirement:** Phase-to-phase short is the most severe fault the inverter can experience (H-12). The NCV57100 DESAT is the primary protection. This test validates the full short-circuit protection chain: DESAT detection → soft turn-off → FLT assertion → safe state. **WARNING:** This test must be conducted at reduced DC link voltage (50 V or less) with appropriate safety barriers and energy-limiting precautions. Even at reduced voltage, fault energy can be destructive if protection fails.

#### C-32: Phase-to-Phase Short (V-W and U-W)

**Objective:** Same as C-31 for V-W and U-W phase combinations.

**Covered:** SG-12 (ASIL C), SG-14 (ASIL C), FSR-13, H-12

**Procedure:** Identical to C-31 but short V-W and U-W combinations separately. Use reduced DC link voltage.

**Pass Criteria:** Same as C-31 for all phase combinations.

**Why this covers the requirement:** All three phase-pair combinations must be independently validated. The DESAT protection must work regardless of which phases are shorted.

#### C-33: Phase-to-DC-Positive Short

**Objective:** Verify protection response when a motor phase shorts to DC link positive rail.

**Covered:** SG-12 (ASIL C), FSR-13, H-12

**Procedure:**

1. System at key-on. DC link at reduced voltage (50 V).
2. Apply short from Phase U to DC+ using low-inductance contactor.
3. If the high-side IGBT of Phase U is ON: DESAT should trigger immediately.
4. If the low-side IGBT is ON: shoot-through path created through Phase U winding to DC+. Overcurrent protection must trigger.
5. Verify safe state. Inspect hardware.

**Pass Criteria:** Protection triggers within <10 us. No hardware damage at reduced voltage. Safe state entered. Fault logged.

**Why this covers the requirement:** This fault simulates insulation failure in the motor winding (turn-to-ground fault at DC+ potential). The current path depends on which switch is conducting. Either DESAT or the overcurrent comparator must catch it.

#### C-34: Phase-to-DC-Negative Short

**Objective:** Same as C-33 for phase shorted to DC link negative rail.

**Covered:** SG-12 (ASIL C), FSR-13, H-12

**Procedure:** Identical to C-33 but short Phase U to DC-. Test all three phases.

**Pass Criteria:** Same as C-33.

**Why this covers the requirement:** Motor winding ground fault to DC- is a common failure mode (winding insulation degrades, moisture ingress, mechanical damage). All three phases must be independently validated.

#### C-35: DC Link Capacitor Temperature Monitoring

**Objective:** Verify DC link capacitor temperature sensing and thermal protection.

**Covered:** SG-07 (ASIL B), FSR-08 (extended), H-07

**Procedure:**

1. Install temperature sensor(s) on DC link capacitor bank (thermistor or RTD).
2. Operate system at various load levels while monitoring capacitor temperature.
3. Apply elevated ambient temperature or reduced cooling to raise capacitor temp.
4. Verify warning threshold triggers power derate. Verify critical threshold triggers safe state.
5. Verify temperature reading is stable and accurate (compare with external thermometer).
6. Verify sensor open/short detection (if applicable).

**Pass Criteria:** Temperature reading accurate (±5°C). Warning derate at T<sub>warn</sub> (e.g., 70°C). Critical safe state at T<sub>crit</sub> (e.g., 85°C). Sensor fault detected and safe state entered.

**Why this covers the requirement:** DC link capacitor lifetime decreases exponentially with temperature (Arrhenius law: lifetime halves every 10°C rise). Capacitor overtemperature can lead to electrolyte venting (explosion for electrolytic) or capacitance loss (film capacitors). Monitoring capacitor temperature extends service life and prevents catastrophic failure. **Note:** This may require adding a temperature sensor to the capacitor bank if not already present.

#### C-36: Bearing Current / Class Y Capacitor Effectiveness

**Objective:** Validate that Class Y safety capacitors effectively shunt common-mode bearing currents to ground, protecting motor bearings from electrical discharge machining (EDM).

**Covered:** SG-07 (ASIL B, extended), H-07 (indirect — bearing damage leads to mechanical failure)

**Procedure:**

1. System operating at rated speed and load. Motor shaft grounded through insulated coupling on dyno.
2. Measure common-mode voltage at motor terminals using high-bandwidth differential probe.
3. Measure shaft voltage (voltage between motor shaft and ground) using carbon brush or capacitive coupling.
4. Calculate bearing voltage ratio (BVR = V<sub>shaft</sub> / V<sub>common-mode</sub>). Target: BVR < 0.1 with Class Y caps installed.
5. Temporarily remove Class Y capacitors. Verify BVR increases significantly (>0.3).
6. Reinstall capacitors. Verify BVR returns to low value.

**Pass Criteria:** With Class Y capacitors: BVR < 0.1. Shaft voltage < 1 V peak (below bearing oil film breakdown threshold of ~5–15 V). Without capacitors: BVR > 0.3 (confirms capacitors are effective). No bearing damage after extended operation.

**Why this covers the requirement:** PWM inverters generate high dv/dt common-mode voltage that capacitively couples to the motor shaft. When shaft voltage exceeds the bearing lubricant's dielectric strength (~5–15 V), electrical discharge occurs through the bearing, pitting the raceways. This is called Electrical Discharge Machining (EDM). Class Y safety capacitors (rated for line-to-ground use, fail-safe) provide a low-impedance path for common-mode current to flow to ground, bypassing the bearings. This test validates the capacitor effectiveness by measuring the bearing voltage ratio. This is a long-term reliability test, not an immediate safety test, but bearing failure at high speed is a safety concern.

#### C-37: STM32 HSE Clock Failure

**Objective:** Verify system response when the external high-speed crystal (HSE) fails.

**Covered:** SG-13 (ASIL D), FSR-15, H-13, H-14

**Procedure:**

1. System operating normally with HSE as clock source.
2. Disable HSE (remove crystal drive signal or switch off oscillator circuit).
3. Verify STM32 CSS (Clock Security System) detects HSE failure and switches to HSI (internal RC).
4. Verify system enters safe state (operating on HSI at reduced accuracy is not acceptable for motor control).
5. Verify fault logged. Verify system does not attempt full-load operation on HSI.

**Pass Criteria:** CSS detects HSE loss within <1 ms. System enters safe state. Fault logged. No continued motor control on degraded clock.

**Why this covers the requirement:** HSE failure (crystal crack, cold solder joint, EMI damage) causes the MCU to lose its accurate timebase. FOC requires precise PWM timing; operating on the internal HSI RC oscillator (±1% accuracy vs. ±30 ppm for crystal) causes current control degradation and potential instability. The CSS must detect this and force safe state.

#### C-38: STM32 PLL Unlock

**Objective:** Verify safe state when PLL loses lock (unstable system clock).

**Covered:** SG-13 (ASIL D), FSR-15, H-13

**Procedure:**

1. System operating with PLL providing 550 MHz system clock.
2. Introduce Vdd noise or temperature transient that causes PLL to lose lock.
3. Alternatively, use debug interface to momentarily change PLL configuration to unstable values.
4. Verify system detects PLL unlock (if monitoring available) or watchdog triggers due to timing chaos.
5. Verify safe state entered.

**Pass Criteria:** PLL unlock detected or watchdog triggers within <50 ms. Safe state entered. System resets and runs POST.

**Why this covers the requirement:** PLL unlock causes the system clock frequency to drift unpredictably. All timing-dependent functions (PWM, ADC sampling, control loop) become erratic. If explicit PLL lock monitoring is not implemented, the watchdog serves as the fallback.

#### C-39: Flash Bit Rot / Corruption Detection

**Objective:** Verify boot CRC (FSR-19) detects flash corruption from bit rot, EMI, or wear.

**Covered:** SG-01 (ASIL D), SG-13 (ASIL D), FSR-19, H-15

**Procedure:**

1. Corrupt a single bit in safety-critical flash region (using debugger).
2. Corrupt multiple bits in the same word.
3. Corrupt calibration data (torque LUT, temperature thresholds).
4. Corrupt the CRC checksum itself (but leave code intact).
5. Power cycle after each corruption. Verify boot behavior.

**Pass Criteria:** All corruption scenarios detected at boot. PWM enable prevented. Fault logged with distinct DTC for code corruption vs. calibration corruption. System remains in safe state until reprogrammed.

**Why this covers the requirement:** Flash memory is subject to: (1) bit rot from cosmic radiation and charge leakage over time, (2) corruption from EMI during write operations, (3) wear from repeated erase/program cycles. FSR-19 prevents the system from operating with corrupted firmware that could modify safety-critical behavior. This test validates the CRC catches all realistic corruption patterns.

#### C-40: GPIO Stuck-At Fault Simulation

**Objective:** Verify system responds safely when critical GPIO pins are stuck-at-1 or stuck-at-0.

**Covered:** SG-01 (ASIL D), SG-13 (ASIL D), FSR-14, H-13, H-14

**Procedure:**

1. System operating normally. Identify critical GPIOs: PWM enable, breakpoint, HVIL, throttle ADC inputs, FLT input.
2. Use debug interface to force each critical GPIO to stuck-at-1, then stuck-at-0.
3. PWM enable stuck high: verify breakpoint override still works.
4. PWM enable stuck low: verify no PWM output (safe but non-functional).
5. Breakpoint input stuck at non-fault level: verify watchdog still provides safe-state path.
6. HIL input stuck: verify other safety paths are not compromised.

**Pass Criteria:** No stuck-at fault creates an unsafe condition. At least one independent safe-state path remains functional for each fault. With six SSO pathways and 1oo2 power kill, no single-point fault can prevent safe state entry.

**Why this covers the requirement:** GPIO stuck-at faults occur from: broken traces, damaged I/O cells, solder bridges, connector faults. The analysis identifies which stuck-at faults are safe and which create dangerous single-point failures. This guides hardware redesign (add pull-ups/pull-downs, redundant paths) and documents acceptable vs. unacceptable fault modes.

#### C-41: ADC Reference Voltage Drift

**Objective:** Verify ADC reference stability and detect reference drift that causes sensor misreading.

**Covered:** SG-01 (ASIL D), FSR-02, H-01, H-06

**Procedure:**

1. System operating. Apply known precision voltage to one ADC channel (calibration reference).
2. Monitor ADC reading of precision reference over temperature sweep (25°C to 85°C).
3. Verify ADC reading stays within ±1% of expected value.
4. If STM32 has internal Vrefint channel: monitor Vrefint reading as proxy for ADC reference stability.
5. Apply external Vref drift (using programmable reference) and verify FSR-02 (plausibility) catches resulting sensor errors.

**Pass Criteria:** ADC reference stable within ±1% over temperature. Vrefint reading in expected range. If Vref drifts beyond ±2%: plausibility check detects sensor mismatch and enters safe state.

**Why this covers the requirement:** ADC reference drift (from temperature, aging, or supply noise) causes ALL analog sensor readings to scale proportionally. This is dangerous because the throttle and current sensors will read incorrectly in a correlated way. The plausibility check (FSR-02) is the primary defense — it compares commanded vs. measured current, which should reveal the scaling error.

#### C-42: SPI Communication Fault (MAX22530 Isolated ADC)

**Objective:** Verify system handles SPI communication failures with the isolated voltage measurement ADC.

**Covered:** SG-10 (ASIL B), FSR-11, H-10

**Procedure:**

1. System operating with MAX22530 providing DC link voltage readings.
2. Inject SPI faults: SCLK stuck, MISO open, CSN stuck high, corrupted MOSI data.
3. Verify DC link voltage reading becomes invalid or stale.
4. Verify system detects loss of valid DC link data and enters safe state.
5. Verify DC link overvoltage protection still functions via independent path (if any).

**Pass Criteria:** SPI fault detected within <200 ms. Safe state entered. No operation with invalid DC link measurement. Fault logged.

**Why this covers the requirement:** The MAX22530 provides isolated DC link and phase voltage measurements via SPI. An SPI fault (broken trace, connector issue, EMI) means the system loses DC link voltage visibility. Operating without DC link voltage data risks overvoltage events going undetected. The system must detect the communication loss and enter safe state.

#### C-43: CAN Bus Off State and Recovery

**Objective:** Verify system handles CAN bus-off state correctly (TEC > 255).

**Covered:** SG-01 (ASIL D), FSR-17, H-03

**Procedure:**

1. System operating with active CAN communication.
2. Inject CAN errors at high rate to force TEC (Transmit Error Counter) above 255, triggering bus-off.
3. Verify system detects bus-off state.
4. Verify safe-state defaults applied (as if heartbeat lost).
5. Stop error injection. Verify CAN peripheral recovers per ISO 11898-1 (128 occurrences of 11 consecutive recessive bits).
6. Verify system resumes normal operation only after key cycle (not automatic).

**Pass Criteria:** Bus-off detected. Safe state entered. Recovery follows ISO 11898-1 protocol. System does not auto-resume without key cycle.

**Why this covers the requirement:** CAN bus-off occurs during severe bus faults (shorted CAN_H/CAN_L, failed transceiver on another node, broken termination). The system must not continue operating as if CAN is healthy. The bus-off state is the CAN controller's built-in self-protection. The test validates that the VCU treats bus-off as a communication loss and applies safe defaults.

#### C-44: Watchdog Starvation Under CPU Load

**Objective:** Verify watchdog triggers when CPU is overloaded and cannot service WDT in time.

**Covered:** SG-13 (ASIL D), FSR-15, H-13, H-14

**Procedure:**

1. System operating normally with WDT timeout = 50 ms.
2. Introduce CPU load spike: heavy interrupt load, DMA saturation, or floating-point-intensive task.
3. Verify watchdog service is missed. Verify WDT triggers reset.
4. Verify safe state (SSO) is maintained during reset and POST.
5. Verify system does not produce tractive effort during the reset window.

**Pass Criteria:** WDT triggers within specified window. System resets. POST executes. No tractive effort during reset. Safe state maintained throughout.

**Why this covers the requirement:** A software bug (infinite loop, priority inversion, stack overflow) or EMI-induced code execution fault can prevent the WDT from being serviced. The WDT is the last line of defense against runaway software. This test validates that the WDT is not accidentally masked by normal CPU load (windowed watchdog helps here) and that it triggers reliably when the CPU is truly stuck.

#### C-45: IGBT Thermal Runaway Profile

**Objective:** Verify thermal protection catches IGBT thermal runaway before junction temperature exceeds T<sub>j,max</sub>.

**Covered:** SG-07 (ASIL B), FSR-08, H-07

**Procedure:**

1. System operating at overload condition (e.g., 120% rated current with reduced cooling).
2. Monitor all three IGBT temperature sensors and compare against junction temperature estimation (from V<sub>CE(sat)</sub> measurement or thermal model).
3. Verify progressive derate occurs as temperature rises: 100% → 80% → 50% → 20%.
4. Continue until critical temperature reached. Verify SSO entered.
5. Verify junction temperature never exceeds T<sub>j,max</sub> (typically 150°C or 175°C).

**Pass Criteria:** Progressive derate observed. SSO at critical temperature. T<sub>j</sub> < T<sub>j,max</sub> at all times. 2oo3 voter works correctly (test with one sensor reading artificially low to simulate sensor fault).

**Why this covers the requirement:** H-07 is thermal runaway leading to fire. The 2oo3 voter must correctly identify the true temperature even if one sensor is faulty. The progressive derate gives the rider time to react (reduce load, find shade) before SSO. The test validates the full thermal protection chain under realistic overload conditions.

#### C-46: Pre-Charge and Inrush Current Validation

**Objective:** Verify pre-charge sequence limits inrush current into DC link capacitors.

**Covered:** SG-10 (ASIL B), FSR-11, H-10, H-12

**Procedure:**

1. DC link capacitors fully discharged.
2. Initiate key-on sequence. Monitor DC link voltage and inrush current.
3. Verify pre-charge resistor (or active pre-charge circuit) limits inrush to acceptable level (<50 A peak for <100 ms).
4. Verify pre-charge completes before contactor closes (voltage > 95% of battery voltage).
5. Verify main contactor only closes after pre-charge complete.
6. Verify failed pre-charge (open resistor, shorted capacitor) is detected and prevents contactor closure.

**Pass Criteria:** Inrush current < specified limit. Pre-charge completes within timeout (<3 s). Contactor only closes after pre-charge verified. Failed pre-charge detected and safe state entered. No sparking/welding on contactor closure.

**Why this covers the requirement:** Closing the main contactor into discharged capacitors creates massive inrush current (hundreds of amps) that welds contacts, damages capacitors, and trips BMS protection. The pre-charge sequence limits this current to a safe level. A failed pre-charge (open resistor) means the contactor would close into a short-circuit-like condition. This test validates the entire pre-charge control logic.

#### C-47: Active Miller Clamp Validation

**Objective:** Verify NCV57100 active Miller clamp prevents dv/dt-induced false turn-on.

**Covered:** SG-12 (ASIL C), FSR-12, H-12

**Procedure:**

1. System operating at rated DC link voltage. High-side IGBT of Phase U switching at maximum dv/dt.
2. Monitor low-side gate voltage of Phase U during high-side turn-on (the dv/dt couples through Miller capacitance).
3. Verify gate voltage spike is clamped to <2 V by active Miller clamp.
4. Compare with Miller clamp disabled (if test fixture allows): verify gate spike is significantly higher without clamp.
5. Repeat at maximum DC link voltage and maximum temperature.

**Pass Criteria:** Miller-induced gate spike < V<sub>GE(th)</sub> (typically <2 V). No false turn-on observed. Active Miller clamp current within specification.

**Why this covers the requirement:** When the high-side IGBT turns on, the rapid dv/dt across the half-bridge creates a current through the Miller capacitance (C<sub>GC</sub>) that can charge the low-side gate capacitance. If the gate voltage exceeds V<sub>GE(th)</sub>, the low-side IGBT turns on briefly, creating shoot-through. The active Miller clamp provides a low-impedance path to shunt this current, preventing false turn-on. This is especially critical at high DC link voltage where dv/dt is maximum.

#### C-48: Gate Driver Propagation Delay Matching

**Objective:** Verify propagation delay matching across all six gate drivers to prevent skew-induced shoot-through.

**Covered:** SG-12 (ASIL C), FSR-12, H-12

**Procedure:**

1. Apply identical PWM test signal to all six gate driver inputs.
2. Measure propagation delay (input edge to output edge) for each gate driver using oscilloscope.
3. Record turn-on delay (t<sub>d,on</sub>) and turn-off delay (t<sub>d,off</sub>) for all six.
4. Calculate delay mismatch: max(t<sub>d</sub>) − min(t<sub>d</sub>) across all six drivers.
5. Verify mismatch < deadtime setting (typically 1–2 us).

**Pass Criteria:** Propagation delay variation < 100 ns between all six drivers. Delay mismatch < deadtime margin. No driver significantly slower/faster than others.

**Why this covers the requirement:** Propagation delay mismatch between gate drivers can cause one phase to switch before another, creating instantaneous voltage imbalance and potential shoot-through in the phase that switches earliest. For complementary switching (one phase on, another off), delay mismatch can create a window where both are on. This test ensures the hardware switching is well-matched and the deadtime margin is adequate.

#### C-49: PWM Deadtime Verification

**Objective:** Verify deadtime is always present between complementary switching edges.

**Covered:** SG-12 (ASIL C), FSR-12, H-17

**Procedure:**

1. Generate PWM at various duty cycles (5%, 50%, 95%) and switching frequencies.
2. Measure high-side and low-side gate signals of one phase on oscilloscope.
3. Verify deadtime (both gates off) is present between every switching transition.
4. Measure deadtime duration. Verify within programmed value (±10%).
5. Verify no deadtime collapse at minimum or maximum duty cycle.
6. Verify complementary breakpoint disable produces simultaneous OFF (not ON).

**Pass Criteria:** Deadtime present on every transition. Duration = programmed value ±10%. No deadtime collapse at any operating point. Breakpoint disable = both OFF simultaneously.

**Why this covers the requirement:** H-17 is PWM deadtime violation causing shoot-through. TIM1 generates deadtime in hardware, but this test verifies it is actually present on the gate signals (not just in the timer registers). Deadtime collapse at extreme duty cycles is a known timer bug risk. This is a production test that should be run on every unit.

#### C-50: Isolation Barrier Degradation (HV)

**Objective:** Verify reinforced isolation barriers maintain integrity under operating stress.

**Covered:** SG-09 (ASIL A), FSR-10, H-09

**Procedure:**

1. Measure isolation resistance between HV DC link and logic ground using 500 V megohmmeter.
2. Verify >500 Ohm/V per ISO 6469-3 (e.g., >100 kOhm for 200 V system, >250 kOhm for 500 V system).
3. Apply HiPot test at 2 kV AC (or 2.8 kV DC) for 60 seconds across isolation barrier.
4. Verify no breakdown, no corona, leakage current < 5 mA.
5. Repeat after thermal aging (85°C soak for 24 hours).

**Pass Criteria:** Isolation resistance > 500 Ohm/V. HiPot passes without breakdown. Post-thermal isolation resistance > 80% of initial value.

**Why this covers the requirement:** H-09 is HV electrical shock from isolation failure. Isolation barriers (gate driver isolation, DC/DC isolation, CAN isolation) degrade over time from thermal cycling, humidity, and electrical stress. The HiPot test validates the barrier can withstand the rated working voltage plus safety margin. This is a type test (not run on every unit) but must pass before any HV operation.

## 10.4 System-Level Tests

System-level tests exercise complete end-to-end fault scenarios with all hardware and software running in closed-loop FOC control on a dyno or test rig. These tests validate that the integrated system responds correctly to realistic fault conditions.

#### S-01: Unintended Tractive Effort from Throttle Fault

**Objective:** Verify SG-01, FSR-01, FSR-03, FSR-18 — System rejects unintended tractive effort when throttle input is implausible.

**Covered:** SG-01 (ASIL D), H-01, FSR-01, FSR-03, FSR-18

**Setup:** Traction motor on dyno. System in normal operating mode at key-on. DC link bus supply at nominal voltage.

**Procedure:**

1. Apply 50% tractive effort request via both throttle pots (matched). Verify system produces corresponding tractive effort.
2. Inject pot 1 short to +5 V (simulating wiring fault). Pot 2 remains at 50%.
3. Observe system response: should detect >5% discrepancy between pots.
4. Repeat with pot 1 short to GND, pot 1 open circuit, pot 2 drifting (slow ramp offset).
5. Verify limit switch override: apply 50% throttle on both pots, activate limit switch. Verify tractive effort commands to zero regardless of pot values.

**Pass Criteria:** All fault injections detected within <100 ms. Tractive effort ramped to zero at ≤500 Nm/s (FSR-03). Safe state entered. Fault logged with correct DTC. Limit switch override has absolute priority over all pot values.

**Why this covers the requirement:** This is the highest-severity hazard (H-01). The test validates the complete chain from sensor fault through detection to safe state entry. The dual-pot redundancy with discrepancy check (FSR-01) is the primary defense. The limit switch (FSR-18) provides the independent override. The rate limiter (FSR-03) prevents a step change if one pot fails high and the other is trusted.

#### S-02: Unintended Reverse Tractive Effort

**Objective:** Verify SG-02, FSR-04 — Reverse tractive effort is rejected when speed > 0.

**Covered:** SG-02 (ASIL B), H-02, FSR-04

**Setup:** Traction motor spinning forward on dyno (simulated 30 mph equivalent).

**Procedure:**

1. Rotate traction motor forward at >100 rpm.
2. Command reverse tractive effort (negative torque request from rider interface).
3. Verify system clamps negative tractive effort to zero.
4. Bring motor to stop (<10 rpm). Command reverse tractive effort with reverse gear explicitly selected. Verify reverse tractive effort is permitted.
5. Bring motor to stop. Command reverse tractive effort WITHOUT reverse gear selected. Verify request is rejected.

**Pass Criteria:** Reverse tractive effort rejected when speed > 100 rpm. Reverse permitted only when stationary AND reverse explicitly selected. All other cases → zero tractive effort, fault logged.

**Why this covers the requirement:** H-02 is rearward tip-over at standstill or low speed. FSR-04 prevents reverse tractive effort at any forward speed. The two-condition interlock (stationary + selected) prevents inadvertent reverse from a software glitch or sensor fault.

#### S-03: Sudden Loss of Tractive Effort (Highway)

**Objective:** Verify SG-03, FSR-05 — Tractive effort removed gradually to prevent rear-end collision hazard.

**Covered:** SG-03 (ASIL C), H-03, FSR-05

**Setup:** Traction motor on dyno at 80% load (simulating highway cruise). System in normal closed-loop control.

**Procedure:**

1. Stabilize system at 80% rated tractive effort. Record baseline.
2. Trigger a non-critical fault that requires safe state entry (e.g., minor temperature excursion, non-critical CAN timeout).
3. Measure tractive effort ramp-down rate using dyno torque sensor.
4. Repeat with critical fault (encoder loss, DESAT) that demands immediate SSO.

**Pass Criteria:** Non-critical faults: tractive effort ramps down at ≤200 Nm/s before SSO. Critical faults: SSO permitted within <50 ms if safety timing demands (FSR-05 exception). Dyno shows smooth torque decay, no step change.

**Why this covers the requirement:** H-03 is sudden loss of tractive effort at highway speed. The primary danger is the following vehicle colliding with the motorcycle. A step torque removal causes rapid deceleration that following traffic may not anticipate. FSR-05 requires gradual removal (≤200 Nm/s) so the rider can react (activate hazard lights, move to shoulder) and following vehicles have time to adjust. This test directly measures the ramp rate to validate it meets the requirement.

#### S-04: Loss of Tractive Effort During Cornering at Lean

**Objective:** Verify SG-03, FSR-05 — Gradual tractive effort removal during cornering does not cause the motorcycle to stand up and run wide.

**Covered:** SG-03 (ASIL C), H-03a, FSR-05

**Setup:** This test validates the **rate limiter** behavior. Full cornering validation requires vehicle-level testing or motorcycle dynamics simulation (see Known Limitations, Section 10.9). The dyno test validates the controllable portion: torque ramp rate.

**Procedure:**

1. Stabilize system at 60% rated tractive effort on dyno.
2. Inject a fault requiring tractive effort removal while motor is loaded.
3. Measure actual torque ramp-down rate. Verify ≤200 Nm/s.
4. Simulate repeated fault/recovery cycles to ensure ramp-down is consistent.

**Pass Criteria:** Torque ramp-down rate never exceeds 200 Nm/s. No step changes observed. SSO only after ramp completes (unless <50 ms safety exception applies).

**Why this covers the requirement (Motorcycle Dynamics Rationale):** H-03a is distinct from H-03. When a motorcycle is cornering at lean angle, the rider balances lateral acceleration (cornering force) against gravity and tire friction. The rear tire contact patch provides both lateral cornering force and longitudinal propulsive force. At full lean on a racetrack, the tire is operating at or near its friction limit.

When tractive effort is suddenly removed (step to zero), the longitudinal force component at the rear contact patch collapses instantly. This has two destabilizing effects: (1) the motorcycle chassis experiences a forward weight transfer (dive) that reduces rear tire normal force, which reduces available lateral friction; (2) the loss of rearward propulsive force causes the motorcycle to **stand up** (reduce lean angle) because the lateral force requirement changes with the reduced longitudinal slip. The rider, committed to a cornering line at full lean, now finds the motorcycle running wide toward the outside of the turn (toward oncoming traffic, barriers, or track edge).

Unlike a four-wheeled vehicle where loss of drive simply causes deceleration, a leaned motorcycle's trajectory is **coupled to its propulsive state**. The rider has zero margin to recover: both hands are committed to handlebar control, the body is positioned for the corner, and the tire is already at its friction limit. There is no "catching" a run-wide event at 40-degree lean and 80 mph.

FSR-05 limits the ramp-down rate to ≤200 Nm/s precisely to keep the change in longitudinal force gradual enough that the rider can modulate lean angle progressively. This test validates the ramp rate on the dyno. The vehicle-level effect (does the bike run wide?) can only be validated by track testing with an experienced rider, which is noted as a limitation.

#### S-05: Uncommanded Regenerative Braking

**Objective:** Verify SG-05, FSR-06 — Uncommanded regenerative braking tractive effort is detected and rejected.

**Covered:** SG-05 (ASIL C), H-05, FSR-06

**Setup:** Traction motor on dyno in motoring mode (simulating downhill or coasting with no rider brake request).

**Procedure:**

1. Spin traction motor at 40 mph equivalent. Set rider brake request to zero (no regen requested).
2. Inject a software fault causing negative Id current (regen) to be commanded.
3. Verify FSR-06 monitors the magnitude of regenerative braking tractive effort.
4. Observe detection threshold at 10 Nm and response time.

**Pass Criteria:** Uncommanded regen >10 Nm detected within <200 ms. Safe state entered. Friction brakes remain available (independent system).

**Why this covers the requirement:** H-05 is unexpected deceleration on a wet or slippery surface during cornering. The one-pedal regen strategy is intentionally mild to prevent wheel lock. However, a software fault could command excessive regen. FSR-06 provides the independent monitor. This test validates that the monitor catches software-induced uncommanded regen.

#### S-06: Full Load Continuous Operation with Thermal Camera Survey

**Objective:** Identify all thermal hotspots under sustained full-load operation using infrared thermography.

**Covered:** SG-07 (ASIL B), FSR-08, H-07 (extended to all components)

**Setup:** Traction motor on dyno. Infrared thermal camera (320x240 minimum, <50 mK NETD). Emissivity-calibrated targets on key surfaces. Ambient temperature 25°C with controlled airflow.

**Procedure:**

1. Apply thermal emissivity tape or paint (ε = 0.95) to: IGBT modules, DC link capacitors, gate driver PCBs, current sensors, DC/DC converter, STM32 MCU, CAN transceivers, busbars, contactors/relays, motor connection terminals.
2. Begin operation at 25% rated load. Run for 15 minutes. Capture thermal image.
3. Increase to 50% load. Run 15 minutes. Capture thermal image.
4. Increase to 75% load. Run 15 minutes. Capture thermal image.
5. Increase to 100% rated load. Run for minimum 60 minutes (or until thermal steady-state). Capture thermal images every 10 minutes.
6. Record maximum temperature of every component. Flag any component exceeding 80% of its rated maximum temperature.
7. Identify unexpected hotspots (poor solder joints, undersized traces, concentrated current paths).

**Pass Criteria:** All component temperatures < 80% of rated maximum at 100% load steady-state. No unexpected hotspots >10°C above surrounding area. IGBT T<sub>case</sub> < 100°C. DC link capacitor T < 70°C. Busbars < 80°C.

**Why this covers the requirement:** Thermal design validation cannot rely solely on temperature sensors at a few points. A thermal camera reveals: poor solder joints (higher resistance = localized heating), current crowding in traces, inadequate heatsink contact, unexpected coupling between hot components. Full-load steady-state is the worst-case thermal scenario. Identifying hotspots during bench testing prevents field failures. This test should be run on the first production-representative unit and repeated after any hardware revision.

#### S-07: Thermal Cycling — IGBT and DC Link Capacitor

**Objective:** Validate thermal protection and mechanical integrity under rapid temperature changes.

**Covered:** SG-07 (ASIL B), FSR-08, FSR-11, H-07, H-10

**Setup:** Traction motor on dyno. Temperature chamber or local heating/cooling for IGBT and capacitor. Thermal camera monitoring.

**Procedure:**

1. Cold start from −20°C (or minimum expected ambient). Verify system starts and operates correctly.
2. Ramp to full load while monitoring IGBT temperature rise rate.
3. Verify temperature sensors read correctly at low temperature (check for frozen calibration).
4. Rapid transition: full load hot (>80°C) → immediate shutdown → immediate restart. Verify no false fault from thermal shock.
5. Repeat 10 thermal cycles: cold → full load hot → cooldown. Monitor for solder joint degradation.

**Pass Criteria:** System starts at −20°C. All temperature readings accurate across range. No false faults during thermal shock. No measurable increase in thermal resistance after 10 cycles.

**Why this covers the requirement:** Thermal cycling causes mechanical stress from CTE (coefficient of thermal expansion) mismatch: solder joints, wire bonds, substrate attachments. Repeated cycling can crack solder joints (increasing thermal resistance) or delaminate substrates. The 10-cycle test is a screening test; the real validation comes from extended thermal cycling (500+ cycles) which is noted as a future environmental test.

#### S-08: Regenerative Braking at Maximum Power

**Objective:** Verify regen system handles maximum regenerative braking power without overvoltage or instability.

**Covered:** SG-05 (ASIL C), SG-10 (ASIL B), FSR-06, FSR-11, H-05, H-10

**Setup:** Traction motor driven by external dyno at maximum rated speed. DC link bus supply with overvoltage protection. Battery simulator or resistive load bank capable of absorbing max regen power.

**Procedure:**

1. Spin traction motor at rated speed via external dyno.
2. Apply maximum regenerative braking torque command.
3. Monitor DC link voltage during regen. Verify it stays below warning threshold.
4. Gradually increase regen torque until DC link reaches warning threshold. Verify regen is limited.
5. If battery simulator has limited absorption: verify system limits regen to what the battery can accept.
6. Verify smooth torque transition into and out of regen (no oscillation).
7. Repeat at multiple speeds including field-weakening region.

**Pass Criteria:** DC link voltage stable during max regen. No overvoltage events. Regen smoothly limited at battery capacity. No torque oscillation. All temperature limits respected.

**Why this covers the requirement:** Maximum regen power is a worst-case scenario for DC link overvoltage (H-10) and potentially for uncommanded regen (H-05) if the control loop oscillates. This test validates the complete regen control chain: torque command → Id current control → DC link voltage feedback → overvoltage limiter. Field-weakening region regen is particularly challenging because the back-EMF exceeds DC link voltage.

#### S-09: Field Weakening Region Operation

**Objective:** Verify stable operation in field-weakening region (speed above base speed).

**Covered:** SG-06 (ASIL C), FSR-07, H-06

**Setup:** Traction motor on dyno capable of exceeding base speed.

**Procedure:**

1. Operate at base speed with rated torque.
2. Increase speed above base speed. Verify control transitions to field weakening.
3. Verify torque is correctly derated above base speed (T ∝ 1/ω).
4. Operate at maximum field-weakening speed for 10 minutes.
5. Verify current magnitude does not exceed rated value (even though torque is lower).
6. Verify thermal limits respected at all speeds.

**Pass Criteria:** Smooth transition to field weakening. Correct torque derate. No current overshoot. Stable operation at max speed. Temperatures within limits.

**Why this covers the requirement:** Field weakening requires injecting negative Id current to oppose the permanent magnet flux. Incorrect Id control can cause overcurrent (H-06) or loss of current control. This is a performance validation test that indirectly verifies current control stability at operating extremes.

#### S-10: Startup Sequence Validation

**Objective:** Verify correct sequence from key-on to ready-to-drive.

**Covered:** SG-13 (ASIL D), FSR-16, H-13

**Setup:** Complete system with DC link bus supply, BMS simulator, IO board simulator.

**Procedure:**

1. System fully powered down. DC link discharged.
2. Turn key to ON. Verify sequence: (a) STM32 boots, (b) POST executes, (c) pre-charge starts, (d) DC link charges to >95%, (e) contactor closes, (f) gate driver self-test, (g) encoder validated, (h) CAN communication established, (i) READY indication.
3. Measure time for each phase. Total time key-on to ready < 5 s.
4. Verify tractive effort is NOT available until READY state.
5. Inject fault during each phase (e.g., open HVIL during pre-charge) and verify sequence aborts to safe state.

**Pass Criteria:** Sequence executes in correct order every time. Tractive effort only available in READY state. Fault at any phase aborts to safe state. Total startup time < 5 s.

**Why this covers the requirement:** The startup sequence is the most safety-critical operational phase because the system transitions from unpowered to high-voltage active. Any fault during startup (HVIL open, failed POST, bad encoder) must prevent the system from reaching READY. This test validates the state machine and all transition guards.

#### S-11: Shutdown Sequence Validation

**Objective:** Verify safe shutdown from any operating state.

**Covered:** SG-13 (ASIL D), FSR-05, H-13

**Setup:** System operating at various load levels on dyno.

**Procedure:**

1. Operate at 100% load. Turn key OFF. Verify: (a) tractive effort ramps to zero at ≤200 Nm/s, (b) PWM disabled, (c) contactor opens, (d) DC link discharged via the external discharge resistor per service procedure (no onboard bleeder — the bank remains at bus voltage for hours after shutdown), (e) gate driver supplies disabled.
2. Repeat at 50% load, 25% load, regen mode.
3. Verify abrupt key-off (emergency shutdown) still results in safe state (may bypass ramp-down for <50 ms safety).
4. Verify system cannot be restarted without full key cycle (OFF → ON).

**Pass Criteria:** Safe shutdown from all operating points. Tractive effort ramped (not stepped) where timing allows. PWM disabled before contactor opens. DC link discharged via the external discharge resistor per service procedure (no onboard bleeder; bank remains at bus voltage until discharged).

**Why this covers the requirement:** Shutdown is the second most safety-critical phase. An incorrect sequence (contactor opens before PWM disable = arcing, DC link not discharged = shock hazard) creates immediate danger. The ramp-down requirement (FSR-05) applies here too — the rider expects gradual deceleration when turning the key off.

#### S-12: Key-Cycle Stress Test

**Objective:** Verify reliability of startup/shutdown cycling over many repetitions.

**Covered:** SG-13 (ASIL D), FSR-16, H-13

**Setup:** Automated key-switch cycling. System on dyno at no-load.

**Procedure:**

1. Automated key cycle: ON (5 s) → OFF (5 s) → repeat.
2. Run 1000 cycles.
3. Log every POST result, every startup time, every fault.
4. After 1000 cycles: run full functional test (C-01 through C-05). Compare against baseline.

**Pass Criteria:** 1000 cycles with zero failures. All POST results pass. Startup time variation < 10%. No degradation in functional test results.

**Why this covers the requirement:** Key cycling causes wear on: contactor (inrush every cycle), DC link capacitors (charge/discharge), gate driver supplies (startup stress), connectors (thermal cycling), flash memory (if parameters are written at shutdown). This is a life-test screening that catches infant mortality failures.

#### S-13: Power Dip Ride-Through (Coast-Through)

**Objective:** Verify system survives brief DC link power interruptions without unsafe behavior.

**Covered:** SG-03 (ASIL C), FSR-05, FSR-11, H-03

**Setup:** System on dyno. Programmable DC link supply with interruption capability.

**Procedure:**

1. System operating at 50% load.
2. Interrupt DC link for 1 ms. Verify system rides through (DC link capacitors maintain voltage).
3. Interrupt DC link for 10 ms. Verify system detects undervoltage and enters safe state gracefully.
4. Interrupt DC link for 100 ms. Verify safe state entered. Verify no restart without key cycle.
5. Repeat with load at 25%, 75%, 100%.
6. Repeat with interruption during regen (current flowing into DC link).

**Pass Criteria:** Short dips (<5 ms): ride-through. Longer dips: safe state with gradual ramp-down. No erratic behavior at any dip duration. DC link capacitor energy sufficient for safe shutdown sequencing.

**Why this covers the requirement:** Real-world power interruptions occur from: loose battery terminals, vibration-induced connector movement, BMS contactor bounce, load dump events. The DC link capacitors provide ride-through energy for brief interruptions. This test validates the capacitor sizing and the undervoltage response (FSR-21) at the boundary between ride-through and safe-state.

#### S-14: Reverse Tractive Effort at Standstill

**Objective:** Verify reverse tractive effort functions correctly when explicitly requested at standstill.

**Covered:** SG-02 (ASIL B), FSR-04, H-02

**Setup:** Traction motor on dyno (can rotate freely in both directions).

**Procedure:**

1. Motor stationary (<10 rpm). Reverse gear selected.
2. Apply 25% reverse tractive effort. Verify motor rotates backward at expected speed.
3. Apply 50% reverse tractive effort. Verify stable operation.
4. Deselect reverse while moving backward. Verify tractive effort ramps to zero.
5. Select reverse while motor is spinning forward (coasting). Verify request rejected until speed < 10 rpm.

**Pass Criteria:** Reverse only permitted when stationary AND selected. Tractive effort correctly proportional to throttle. Rejection of reverse while moving forward. Smooth transitions.

**Why this covers the requirement:** This is the positive test for reverse functionality (complement to S-02 which tests the negative case). Reverse is a legitimate operating mode but must be strictly controlled to prevent H-02 (unintended reverse).

#### S-15: Overspeed Protection

**Objective:** Verify system limits maximum motor speed to prevent mechanical overspeed damage.

**Covered:** SG-06 (ASIL C), FSR-07, H-06

**Setup:** Traction motor on dyno capable of exceeding rated maximum speed.

**Procedure:**

1. Command acceleration toward rated maximum speed.
2. Verify speed limit is enforced (tractive effort reduced to zero at speed limit).
3. Attempt to exceed speed limit with external dyno assistance. Verify system resists overspeed (regen if needed).
4. Verify encoder-derived speed agrees with back-EMF-derived speed (plausibility).
5. Verify overspeed fault is logged.

**Pass Criteria:** Speed does not exceed programmed limit by >5%. Tractive effort reduced (not suddenly cut) as speed approaches limit. Overspeed fault logged. No mechanical damage.

**Why this covers the requirement:** Mechanical overspeed of the traction motor can cause rotor burst (permanent magnet disintegration, centrifugal failure). The speed limit is a hard limit that must be enforced even with external forcing (downhill, towed).

#### S-16: Modulation Scheme Transition During Acceleration — Torque Blip

**Objective:** Verify modulation scheme transitions do not produce perceptible torque disturbances during acceleration. Transition gating logic must inhibit switches during high di/dt.

**Covered:** SG-03 (ASIL C), FSR-05, H-03, H-03a

**Setup:** Traction motor on dynamometer with high-bandwidth torque transducer (≥10 kHz bandwidth). DC link at nominal voltage. Automatic modulation map enabled with at least 3 adjacent schemes (e.g., ARSVPWM → SVPWM → SHEPWM).

**Procedure:**

1. Command smooth acceleration from standstill through the speed range that triggers automatic scheme transitions.
2. Record torque transducer output at ≥20 kSPS during each scheme transition.
3. Measure peak torque deviation from pre-transition mean during the crossfade window.
4. Repeat with manual scheme switch commanded via RTE during hard acceleration (>80% torque request).
5. Verify the gating logic rejects or delays the manual switch during high di/dt.
6. Repeat at multiple operating temperatures (25°C, 60°C, 85°C ambient).

**Pass Criteria:** Automatic transitions produce peak torque deviation <2% of rated torque. Manual switches during hard acceleration are either gated (delayed until di/dt falls) or produce <5% deviation with crossfade. No fault trips during any transition. No audible tonal change at transition point.

**Fail Criteria:** Torque deviation >5% on automatic transition; ungated manual switch produces perceptible torque blip; false fault trip during transition.

**Why this covers the requirement:** H-03 and H-03a (sudden loss of tractive effort) are triggered by any abrupt torque change. Modulation transitions change the harmonic structure of the voltage output, which can momentarily disturb the current controller. The bumpless crossfade and di/dt gating are the safety mechanisms that prevent this disturbance from becoming a hazard.

#### S-17: Modulation Scheme Transition During Regenerative Braking — Torque Blip

**Objective:** Verify modulation scheme transitions do not produce perceptible torque disturbances during regenerative braking. Uncommanded torque disturbance during regen must be detected by FSR-06.

**Covered:** SG-05 (ASIL C), FSR-06, H-05, H-03

**Setup:** Traction motor on dynamometer in speed-controlled mode driving the DUT into regen. High-bandwidth torque transducer. Automatic modulation map enabled.

**Procedure:**

1. Establish steady-state regenerative braking at 50% rated regen torque.
2. Command a scheme switch via RTE during active regen (high di/dt in d-axis current).
3. Record torque transducer output during the transition.
4. Ramp dyno speed through the automatic map boundary that triggers a scheme change during regen.
5. Verify automatic transition behavior at the regen/acceleration crossover point (zero torque crossing).
6. Verify FSR-06 (uncommanded regen monitor) does not false-trip during legitimate scheme transitions.

**Pass Criteria:** Torque deviation during regen transition <3% of rated torque. No false trip of uncommanded regen monitor (FSR-06) during legitimate transitions. Transition at zero-torque crossing is clean (no direction ambiguity).

**Fail Criteria:** Torque blip >5% during regen transition; FSR-06 false trip during legitimate scheme change; torque direction ambiguity at zero crossing.

**Why this covers the requirement:** H-05 (uncommanded regenerative braking) covers any unexpected deceleration. A badly managed modulation transition during regen can produce a torque blip that feels like uncommanded braking. FSR-06 must distinguish between legitimate control transitions and true uncommanded regen faults.

#### S-18: Hysteresis at Modulation Scheme Boundary — No Jitter

**Objective:** Verify hysteresis prevents rapid back-and-forth switching when operating conditions oscillate near a scheme boundary.

**Covered:** SG-03 (ASIL C), H-03

**Setup:** Traction motor on dynamometer. Automatic modulation map with at least one speed-based boundary (e.g., SVPWM at <80% base speed, SHEPWM at >80%). Hysteresis band configurable (default 5%).

**Procedure:**

1. Operate motor at a speed just below the SVPWM→SHEPWM boundary (e.g., 78% base speed with boundary at 80%).
2. Introduce small speed oscillation (±2% of base speed, 1 Hz) via dyno to simulate road speed variation.
3. Count scheme switches over a 60-second interval.
4. Repeat with speed just above the boundary (82%).
5. Repeat with hysteresis band reduced to 1% and then increased to 10%. Verify switching count scales with hysteresis width.
6. Verify no audible jitter (clicking, tonal change) at the boundary under any condition.

**Pass Criteria:** With default 5% hysteresis, zero scheme switches during ±2% speed oscillation at the boundary. With 1% hysteresis, occasional switches are acceptable but must not exceed 1 per 10 seconds. No audible jitter under any test condition.

**Fail Criteria:** Rapid switching (>1 per second) at boundary with default hysteresis; audible jitter; torque ripple correlating with switching frequency.

**Why this covers the requirement:** Operating at a scheme boundary is a common real-world scenario (cruising near a speed threshold). Without hysteresis, every small speed variation would trigger a scheme switch, producing a continuous stream of torque micro-disturbances. This is a degradation of H-03 (sudden loss of tractive effort) — not a single event, but a chronic instability that erodes rider confidence and could cause loss of control.

#### S-19: Full Modulation Map Traversal — End-to-End

**Objective:** Verify the complete automatic modulation map functions correctly across all speed and torque regions with no dead zones, incorrect selections, or fault trips.

**Covered:** SG-03 (ASIL C), H-03, H-03a, H-05

**Setup:** Traction motor on dynamometer. Full automatic modulation map programmed with all planned schemes. High-bandwidth torque transducer.

**Procedure:**

1. Define a modulation map covering the full speed range (0 to field-weakening max) and torque range (zero to rated, both motoring and regen).
2. Command a slow speed sweep (1% base speed per second) from standstill to maximum speed at constant 50% torque.
3. Record active scheme, torque, and DC link voltage throughout the sweep.
4. Verify correct scheme is selected in each region per the map.
5. Verify all transitions are clean (no fault trips, no >2% torque deviation).
6. Repeat sweep at 25% torque and 75% torque.
7. Repeat in reverse direction (deceleration) to verify hysteresis is direction-independent.

**Pass Criteria:** Correct scheme selected in every region. All transitions clean. No fault trips. Torque deviation <2% at every transition. Map behavior is identical in acceleration and deceleration directions.

**Fail Criteria:** Wrong scheme selected in any region; transition causes fault trip; torque deviation >5% at any transition; different behavior accelerating vs. decelerating.

**Why this covers the requirement:** This is the integration test for the entire multi-modulation subsystem. Individual transition tests (S-16, S-17) validate specific boundary crossings; this test validates that all boundaries work together correctly in a realistic driving profile. Any map programming error, incorrect boundary definition, or interaction between adjacent transitions will be caught.

## 10.5 Integration Tests

Integration tests validate the interaction between the traction inverter/VCU and external systems connected via CAN bus and discrete I/O. These tests require the VCU connected to representative CAN nodes (or CAN simulation tools).

#### I-01: CAN1 (BMS) Heartbeat Loss

**Objective:** Verify FSR-17 — BMS CAN loss triggers safe degradation.

**Covered:** SG-01 (ASIL D), H-03, FSR-17

**Setup:** VCU connected to CAN bus with BMS simulator. System in normal operation producing tractive effort.

**Procedure:**

1. Establish normal CAN1 communication with BMS simulator sending heartbeat every 5 s.
2. Stop BMS heartbeat (simulate BMS failure or CAN1 bus fault).
3. Measure time from last heartbeat to safe state entry.
4. Verify safe-state defaults applied: tractive effort restricted to zero.

**Pass Criteria:** Safe state entered within 5 s + margin (<6 s total). Tractive effort ramped to zero (FSR-05). Fault logged. System does not attempt to operate without BMS data. No erroneous tractive effort produced.

**Why this covers the requirement:** The BMS provides cell voltage, temperature, and fault status. Operating without BMS data risks over-discharge, over-temperature, or use of a battery in fault condition. FSR-17 defines the 5-second timeout (external to VCU domain). This test validates the VCU correctly responds to loss of this critical external data source.

#### I-02: CAN2 (IO Board) Heartbeat Loss

**Objective:** Verify FSR-17 — IO board CAN loss triggers safe-state defaults.

**Covered:** SG-01 (ASIL D), H-03, FSR-17

**Setup:** VCU connected to CAN2 with IO board simulator. System in normal operation.

**Procedure:**

1. IO board simulator sending heartbeat every 1 s with brake=off, kickstand=up, valid state.
2. Stop IO board heartbeat.
3. Measure time from last heartbeat to safe state entry.
4. Verify safe-state defaults: brake=pressed, kickstand=down, all external inputs invalid.

**Pass Criteria:** Safe state entered within <1.5 s. Tractive effort zero. Fault logged. Safe-state defaults correctly applied (brake pressed prevents any tractive effort, kickstand down prevents any tractive effort).

**Why this covers the requirement:** The IO board provides brake switch, kickstand switch, and other safety-critical inputs. Loss of this node means the VCU cannot verify these safety conditions. FSR-17 requires assuming the worst-case safe defaults. This is more aggressive than BMS loss (1 s vs. 5 s) because the IO board provides real-time safety interlocks.

#### I-03: Simultaneous Throttle + Brake Request

**Objective:** Verify SG-01, FSR-01, FSR-18 — Brake request always overrides throttle.

**Covered:** SG-01 (ASIL D), H-01, FSR-01, FSR-18

**Setup:** VCU with IO board simulator. System in normal operation.

**Procedure:**

1. Apply 75% tractive effort via throttle pots (both valid, matched).
2. While maintaining throttle, set IO board brake=pressed.
3. Verify tractive effort immediately commands to zero.
4. Release brake. Verify tractive effort returns to rider-requested value (ramped, not step).
5. Repeat with kickstand=down instead of brake.

**Pass Criteria:** Brake or kickstand signal immediately overrides throttle. Tractive effort commands to zero within <100 ms. On release, tractive effort ramps back up at ≤500 Nm/s (no step). Fault logged for kickstand-down-at-speed condition.

**Why this covers the requirement:** This is the fundamental rider override. In any fault scenario where the throttle is stuck on, the rider's brake lever is the last-resort safety mechanism. The test validates that the brake input (via IO board CAN) unconditionally overrides any tractive effort command. The kickstand-down-at-speed is a special case that also demands zero tractive effort (rider likely did not intend to ride with kickstand down).

#### I-04: HVIL Interruption During Operation

**Objective:** Verify SG-09, SG-11, FSR-10 — HVIL interruption triggers immediate HV disconnect sequence.

**Covered:** SG-09 (ASIL A), SG-11 (ASIL B), H-09, H-11, FSR-10

**Setup:** VCU with HVIL loop simulator. System in normal operation producing tractive effort. Contactor control observable.

**Procedure:**

1. System operating at 50% tractive effort. HVIL loop closed (normal).
2. Open HVIL loop (simulate connector disconnect, crash sensor, or interlock fault).
3. Measure time from HVIL open to PWM disable.
4. Verify contactor open request sent to BMS within <50 ms.
5. Verify system does not re-enable PWM until HVIL is restored AND key cycle occurs.

**Pass Criteria:** PWM disabled within <50 ms of HVIL interruption. Contactor open request sent. Safe state entered. No tractive effort produced after HVIL open. Latch requires key cycle to clear (prevents automatic restart in unsafe condition).

**Why this covers the requirement:** HVIL is the primary HV safety interlock. H-09 (HV isolation failure) and H-11 (contactor welding) are both addressed by FSR-10. A crash or connector fault must immediately disconnect HV. The 50 ms timing requirement ensures the response is fast enough to prevent shock or fire in a post-crash scenario. The latch prevents the system from automatically re-energizing HV while the fault condition persists.

#### I-05: DC Link Overvoltage (Regen Event)

**Objective:** Verify SG-10, FSR-11 — DC link bus overvoltage is detected and limited.

**Covered:** SG-10 (ASIL B), H-10, FSR-11

**Setup:** VCU with DC link bus supply. System in regenerative braking operation (traction motor being driven externally to simulate downhill).

**Procedure:**

1. System in regen mode, DC link at nominal voltage.
2. Raise DC link bus voltage gradually (using external supply) toward warning threshold.
3. Verify regen is disabled at warning threshold (prevent further voltage rise).
4. Continue raising DC link to critical overvoltage threshold.
5. Verify SSO entered within <50 ms.

**Pass Criteria:** Regen disabled at warning threshold. SSO at critical threshold within <50 ms. Both events logged with distinct DTCs. No IGBT damage at any point.

**Why this covers the requirement:** H-10 is DC link bus overvoltage from regenerative braking (especially on a fully charged battery or during a long downhill). Excessive DC link voltage can rupture capacitors and IGBTs, creating arc flash and fire. FSR-11 provides two thresholds: warning (disable regen, prevent further rise) and critical (immediate SSO). This test validates both thresholds and response times.

#### I-06: Kickstand Down at Speed

**Objective:** Verify system responds safely to kickstand-down signal while vehicle is moving.

**Covered:** SG-01 (ASIL D), H-01 (indirect), FSR-17

**Setup:** VCU with IO board simulator. System producing tractive effort.

**Procedure:**

1. System operating at 50% tractive effort. IO board reports kickstand=up, speed >10 mph equivalent.
2. IO board reports kickstand=down (simulating mechanical failure or sensor fault).
3. Verify tractive effort commands to zero immediately.
4. Verify fault logged (kickstand down at speed).
5. Verify tractive effort remains zero even if throttle is applied while kickstand=down.

**Pass Criteria:** Tractive effort zero within <200 ms of kickstand-down signal. Fault logged. Tractive effort remains inhibited while kickstand=down. Safe state persists until key cycle.

**Why this covers the requirement:** Riding with the kickstand down is a dangerous condition (can catch on pavement, causing loss of control). The IO board provides the kickstand state via CAN2. This test validates that the VCU correctly interprets this input as a safety interlock and prevents any tractive effort when the kickstand is reported down, regardless of throttle position.

#### I-07: Simultaneous Multiple CAN Node Loss

**Objective:** Verify graceful degradation when multiple external systems fail simultaneously.

**Covered:** SG-01, SG-03, FSR-17

**Setup:** VCU connected to both CAN1 (BMS) and CAN2 (IO board, display, charger) simulators.

**Procedure:**

1. System operating normally. All CAN nodes active.
2. Simultaneously stop all CAN2 node heartbeats (IO board + display + charger + ABS).
3. Verify system enters safe state based on CAN2 timeout (1 s).
4. Restore CAN2. Verify system returns to normal operation after key cycle (does not auto-resume).
5. Simultaneously stop both CAN1 (BMS) and CAN2 (IO board) heartbeats.
6. Verify system enters safe state (whichever timeout fires first: CAN2 at 1 s or CAN1 at 5 s).

**Pass Criteria:** Safe state entered on first heartbeat timeout to fire. No tractive effort produced during any CAN-loss scenario. System does not auto-recover without key cycle. All fault conditions logged with appropriate DTCs.

**Why this covers the requirement:** Multiple simultaneous failures can occur in a crash or severe EMI event. The system must degrade to the safest possible state, not attempt to continue operating with partial external data. The test validates that the most restrictive timeout dominates and that the system requires a key cycle to resume (prevents oscillation between fault and operation).

#### I-08: CAN Bus Fuzzing — Random Frame Injection

**Objective:** Verify system ignores or gracefully handles random/corrupted CAN frames.

**Covered:** SG-01 (ASIL D), FSR-17, H-01, H-15

**Setup:** VCU on CAN bus with fuzzing tool (CANoe, PCAN, or custom tool). System in normal operation.

**Procedure:**

1. Generate random CAN frames at 10% bus load: random IDs (including IDs used by BMS and IO board), random data lengths (0–8 bytes), random payload.
2. Verify VCU does not malfunction, produce tractive effort, or enter safe state from random frames.
3. Increase to 50% bus load with random frames. Verify normal operation continues.
4. Inject frames with correct IDs but corrupted CRC. Verify frames are rejected by CAN controller.
5. Inject frames with valid BMS ID but payload that would indicate impossible values (e.g., cell voltage = 0xFFFF, temperature = 200°C). Verify sanity checks reject impossible values.

**Pass Criteria:** No malfunction from random frames. Valid heartbeat frames still processed correctly. Impossible values rejected. Bus load < 100% maintained.

**Why this covers the requirement:** CAN bus noise, failed nodes, or malicious injection can send unexpected frames. The VCU must be robust against parsing errors that could lead to incorrect tractive effort (H-15). This validates input validation and parser robustness.

#### I-09: CAN Bus Load Test (95% Utilization)

**Objective:** Verify system operates correctly under maximum CAN bus load.

**Covered:** SG-01 (ASIL D), FSR-17, H-03

**Setup:** CAN bus loaded to 95% utilization with background traffic. VCU operating normally.

**Procedure:**

1. Load CAN bus to 95% with frames from multiple simulated nodes.
2. Verify VCU transmits its frames without excessive delay (< 10 ms jitter).
3. Verify VCU receives BMS and IO board frames without dropping critical messages.
4. Verify heartbeat timeout still functions correctly (no false timeouts from bus congestion).
5. Inject a fault requiring safe state. Verify safe state entry is not delayed by bus load.

**Pass Criteria:** System operates normally at 95% bus load. No dropped critical messages. Heartbeat timeout accurate within ±10%. Safe state entry not delayed.

**Why this covers the requirement:** In a multi-node CAN network, bus load can spike during fault conditions (all nodes sending error frames, diagnostics). The VCU must continue receiving its critical messages even under congestion. This test validates CAN message prioritization and receive buffer sizing.

#### I-10: CAN Bus Off Recovery

**Objective:** Verify system correctly recovers from CAN bus-off state.

**Covered:** SG-01 (ASIL D), FSR-17, H-03

**Setup:** VCU on CAN bus with fault injection capability.

**Procedure:**

1. Inject errors on CAN bus to force VCU into bus-off state (TEC > 255).
2. Verify system enters safe state and applies safe defaults.
3. Stop error injection. Allow bus to go idle (11 consecutive recessive bits × 128).
4. Verify VCU CAN peripheral recovers to error-active state.
5. Verify VCU does NOT automatically resume operation (requires key cycle).
6. Perform key cycle. Verify system resumes normal operation.

**Pass Criteria:** Bus-off detected. Safe state entered. Recovery to error-active per ISO 11898-1. No auto-resume. Key cycle returns to normal operation.

**Why this covers the requirement:** Bus-off is a severe CAN fault. The recovery protocol (128 × 11 recessive bits) is defined by ISO 11898-1. The VCU must follow this protocol and only resume after explicit user action (key cycle), not automatically.

#### I-11: Invalid/Corrupted CAN Frame Injection

**Objective:** Verify system handles frames with valid IDs but semantically invalid data.

**Covered:** SG-01 (ASIL D), SG-10 (ASIL B), FSR-17, H-15

**Setup:** VCU on CAN bus with BMS and IO board simulators.

**Procedure:**

1. BMS simulator sends frame with valid ID but impossible cell voltage (>5 V or <0 V). Verify VCU rejects value and uses last-known-good or safe default.
2. BMS sends temperature = −50°C (impossible for operating battery). Verify rejected.
3. IO board sends brake=pressed AND throttle=100% simultaneously. Verify brake wins (tractive effort = 0).
4. IO board sends kickstand=down AND speed=100 mph. Verify zero tractive effort and fault logged.
5. Send frames with correct ID but DLC mismatch (e.g., DLC=8 for a 4-byte message). Verify parser handles correctly.

**Pass Criteria:** All invalid frames rejected. Safe defaults used when valid data unavailable. No tractive effort from invalid inputs. DLC mismatch handled safely.

**Why this covers the requirement:** Semantically invalid data (from a failed node, corrupted memory, or software bug) is more dangerous than random data because it passes CRC and ID filters. The VCU must have range checks, plausibility checks, and consistency checks on all received data.

#### I-12: Display Node Failure

**Objective:** Verify system operates safely when the display/dash node fails.

**Covered:** SG-01 (ASIL D), FSR-17, H-03

**Setup:** VCU on CAN2 with display simulator. System operating.

**Procedure:**

1. System operating normally. Display showing speed, SOC, fault status.
2. Stop display heartbeat.
3. Verify VCU continues operating (display is non-safety-critical).
4. Verify VCU logs display loss as non-critical fault.
5. Verify tractive effort is NOT affected by display loss.

**Pass Criteria:** Display loss does NOT affect tractive effort or safety functions. Fault logged. System continues operating. Display recovery does not require key cycle.

**Why this covers the requirement:** The display is a non-safety-critical node. Its loss must not affect propulsion. This test validates that the heartbeat timeout correctly distinguishes safety-critical nodes (BMS, IO board) from informational nodes (display).

#### I-13: Charger Node Interaction

**Objective:** Verify correct interaction with charger during charging and prevent drive-away-while-charging.

**Covered:** SG-01 (ASIL D), FSR-04, H-01

**Setup:** VCU on CAN2 with charger simulator.

**Procedure:**

1. Charger connected and active (charging in progress). Verify VCU detects charging state.
2. Attempt to command tractive effort while charging. Verify request rejected.
3. Verify interlock: charging active → tractive effort permanently disabled.
4. Charger completes charging (sends completion message). Verify VCU exits charging state.
5. Verify tractive effort available after charger disconnect confirmation.
6. Inject fault: charger stuck "active" after physical disconnect. Verify VCU has timeout to clear charging state.

**Pass Criteria:** No tractive effort during charging. Clean exit from charging state. Timeout for stuck charger message. Drive-away-prevention interlock functional.

**Why this covers the requirement:** Driving away while the charging cable is connected is a severe safety hazard (could damage charger, cable, or vehicle). The charger-active signal must unconditionally disable tractive effort. This test validates the interlock and its timeout behavior.

#### I-14: ABS Node Coordination

**Objective:** Verify correct interaction with ABS module during anti-lock braking events.

**Covered:** SG-05 (ASIL C), FSR-06, H-05

**Setup:** VCU on CAN2 with ABS simulator.

**Procedure:**

1. System in regenerative braking. ABS simulator sends "ABS active" message.
2. Verify VCU immediately reduces or disables regenerative braking (ABS needs wheel slip control).
3. Verify friction brakes remain fully available (independent of regen).
4. ABS sends "ABS inactive." Verify regen smoothly resumes.
5. Verify no tractive effort produced during ABS active period.

**Pass Criteria:** Regen disabled/reduced within <50 ms of ABS active. Smooth resume when ABS inactive. No interference with friction brake operation.

**Why this covers the requirement:** During ABS events, the ABS controller modulates brake pressure to prevent wheel lock. Regenerative braking (which applies torque directly to the wheel) can interfere with ABS slip control. The VCU must yield to ABS during active events. This is a coordination requirement, not a fault case.

#### I-15: BMS Fault Propagation

**Objective:** Verify VCU correctly responds to BMS fault messages.

**Covered:** SG-01 (ASIL D), SG-10 (ASIL B), FSR-17, H-01, H-10

**Setup:** VCU on CAN1 with BMS simulator. System operating.

**Procedure:**

1. BMS sends warning-level fault: cell imbalance > 50 mV. Verify VCU logs warning, derates tractive effort.
2. BMS sends critical fault: cell overvoltage (>4.25 V). Verify VCU immediately disables regen and enters safe state.
3. BMS sends critical fault: cell undervoltage (<2.5 V). Verify VCU immediately enters safe state.
4. BMS sends critical fault: over-temperature (>60°C). Verify VCU derates or enters safe state.
5. BMS sends contactor weld detection. Verify VCU logs fatal fault and prevents restart.

**Pass Criteria:** Warning faults: log + derate. Critical faults: safe state within <200 ms. Contactor weld: fatal fault + lockout. All responses appropriate to fault severity.

**Why this covers the requirement:** The BMS is the authoritative source for battery safety. The VCU must correctly interpret and act on BMS fault messages with appropriate severity. A BMS critical fault must never be treated as a warning.

#### I-16: Multi-Node Simultaneous Fault

**Objective:** Verify system handles simultaneous faults on multiple CAN nodes.

**Covered:** SG-01 (ASIL D), SG-03 (ASIL C), SG-13 (ASIL D), FSR-17, H-01, H-03, H-13

**Setup:** VCU on both CAN buses with all node simulators.

**Procedure:**

1. System operating at 50% tractive effort.
2. Simultaneously: BMS sends critical fault AND IO board stops heartbeat AND ABS sends active.
3. Verify VCU enters safe state (most restrictive response).
4. Verify safe state entry is not delayed by processing multiple faults.
5. Verify all three faults are logged with correct DTCs.
6. Repeat with different combinations of simultaneous faults.

**Pass Criteria:** Safe state entered within single worst-case timeout (not cumulative). All faults logged. No priority inversion (critical fault not delayed by less critical processing).

**Why this covers the requirement:** In a crash or severe EMI event, multiple faults can occur simultaneously. The fault handling must be deterministic: always take the most restrictive action, never get confused by multiple inputs, never delay the safe state entry.

#### I-17: CAN Bus Wiring Fault (Short and Open)

**Objective:** Verify system response to physical CAN bus wiring faults.

**Covered:** SG-01 (ASIL D), FSR-17, H-03

**Setup:** VCU with physical access to CAN bus wiring.

**Procedure:**

1. System operating normally.
2. Short CAN_H to CAN_L. Verify bus-off within error limit. Verify safe state.
3. Restore wiring. Verify bus-off recovery.
4. Open CAN_H (cut wire). Verify error passive then bus-off. Verify safe state.
5. Restore wiring. Verify recovery.
6. Short CAN_H to +12 V. Verify transceiver protection functions. Verify safe state.
7. Short CAN_L to ground. Verify safe state.

**Pass Criteria:** All wiring faults detected. Safe state entered within timeout. No transceiver damage from shorts. Recovery after wiring restored (key cycle required).

**Why this covers the requirement:** Physical CAN wiring faults (chafing, connector corrosion, crash damage) are common in automotive environments. The ISO1042 transceiver has protection against these faults, but the system must still detect the communication loss and enter safe state.

#### I-18: Wake/Sleep Cycle Test

**Objective:** Verify system handles wake-from-sleep and sleep-entry correctly.

**Covered:** SG-13 (ASIL D), FSR-16, H-13

**Setup:** VCU with sleep/wake capability (if implemented).

**Procedure:**

1. System in normal operation. Trigger sleep condition (key off, no activity timeout).
2. Verify graceful shutdown sequence before sleep entry.
3. Verify quiescent current in sleep mode (< 1 mA typical).
4. Trigger wake (key on, CAN wake-up frame, charger connection).
5. Verify full startup sequence executes (POST, pre-charge, etc.).
6. Verify system is in known good state after wake (no stale data from before sleep).

**Pass Criteria:** Sleep entry: graceful shutdown. Sleep current < limit. Wake: full POST executes. No stale data. System behavior identical to cold start.

**Why this covers the requirement:** If sleep/wake is implemented, wake must be treated as a fresh start — not a continuation of the previous session. Stale data (old fault status, old sensor values) must be discarded. This prevents a fault that occurred before sleep from being masked after wake.

## 10.6 Safety Goal Traceability

The following matrix maps each safety goal to the test cases that provide evidence of coverage. A test may cover multiple safety goals; a safety goal may require multiple tests.

**Table 16 — Safety Goal to Test Case Traceability**

| SG | Target | Safety Goal | Covering Tests |
| --- | --- | --- | --- |
| **SG-01** | D | Prevent unintended positive tractive effort | C-01–C-06, C-13, C-14, C-20–C-25, C-39–C-41, C-44, S-01, S-02, I-01–I-03, I-06–I-08, I-11, I-15–I-17, E-08, E-09 |
| **SG-02** | B | Prevent unintended reverse tractive effort | S-02, S-14, C-13 |
| **SG-03** | C | Safe degradation on loss of tractive effort (≤200 Nm/s) | C-22, S-03, S-04, S-10–S-13, I-01, I-02, I-07 |
| **SG-04** | A | Ensure regen availability (friction brakes backup) | C-11 (indirect: system preserves brake function independently) |
| **SG-05** | C | Prevent unintended regenerative braking | S-05, S-08, I-14 |
| **SG-06** | C | Limit max tractive effort to calibrated max | C-07, C-08, C-13, C-41, S-09, S-15 |
| **SG-07** | B | Detect over-temperature, progressively derate | C-09, C-10, C-35, C-45, S-06, S-07, E-04 |
| **SG-08** | C | Detect loss of rotor position → safe state | C-11, C-28, C-29, C-30 |
| **SG-09** | A | Maintain HV isolation | C-50, I-04, E-07, E-12 |
| **SG-10** | B | Detect DC link bus overvoltage | C-12, C-46, I-05, S-08, S-13 |
| **SG-11** | B | Detect HVIL interruption | I-04 |
| **SG-12** | C | Prevent IGBT shoot-through | C-15, C-16, C-17, C-31–C-34, C-47–C-49 |
| **SG-13** | D | Achieve safe state within 200 ms | C-13, C-14, C-15, C-16, C-17, C-19–C-25, C-37, C-38, C-40, C-43, C-44, S-01–S-03, S-10–S-12, I-04, E-01–E-03, E-10, E-11 |
| **SG-14** | C | Detect gate driver fault | C-15, C-16, C-17, C-26, C-27, C-31–C-34 |
| **SG-15** | C | Detect PWM deadtime violations | C-15, C-16, C-49 (DESAT indirectly covers stuck-on) |

## 10.7 Coverage Justification

### 10.7.1 FSR Coverage Matrix

**Table 17 — Functional Safety Requirement Coverage by Test Cases**

| FSR | Requirement Summary | Covering Tests |
| --- | --- | --- |
| FSR-01 | Dual throttle pot discrepancy >5% → safe state | C-01–C-06, S-01, I-03 |
| FSR-02 | Tractive effort command vs. measured current plausibility | C-07, C-08, C-28, C-29, C-30, C-41, S-01 |
| FSR-03 | Tractive effort rate limit ≤500 Nm/s | S-01, S-03 |
| FSR-04 | Reverse interlock (stationary + selected) | S-02, S-14, I-13 |
| FSR-05 | Fault ramp-down ≤200 Nm/s before SSO | S-03, S-04, S-11, S-13 |
| FSR-06 | Uncommanded regen >10 Nm → safe state | S-05, S-08, I-14 |
| FSR-07 | Max tractive effort limit (SW LUT on both MCUs + dual-MCU current monitoring) | C-07, C-08, S-09, S-15 |
| FSR-08 | 3 modules, 1 NTC per module, 2oo3 voting with 100 °C hard cap | C-09, C-10, C-35, C-45, S-06, S-07 |
| FSR-09 | Encoder loss → safe state <100 ms | C-11, C-28, C-29, C-30 |
| FSR-10 | HVIL interruption → PWM disable + contactor request <50 ms | I-04, C-50 |
| FSR-11 | DC link OV monitor: regen disable / SSO | C-12, C-46, I-05, S-08, S-13 |
| FSR-12 | Gate driver complementary inputs, anti-shoot-through | C-15, C-16, C-47–C-49 |
| FSR-13 | NCV57100 DESAT <2 us PWM disable | C-15, C-16, C-31–C-34 |
| FSR-14 | Breakpoint HW PWM disable <10 us | C-13, C-14, C-23–C-25 |
| FSR-15 | Windowed watchdog ≤50 ms | C-13, C-14, C-37, C-38, C-44 |
| FSR-16 | Power-on self-test (POST) | C-16, C-20, S-10, S-12 |
| FSR-17 | CAN heartbeat timeout with safe defaults | I-01, I-02, I-07, I-08–I-11, I-17 |
| FSR-18 | Throttle limit switch independent override | C-06, S-01, I-03 |
| FSR-19 | Boot CRC-32 | C-20, C-39 |
| FSR-20 | ECC RAM: SECDED + double-bit → safe state | C-18, C-19 |
| FSR-21 | DC link undervoltage derate / safe state | C-12, C-22, S-13 |

### 10.7.2 Hazard Coverage Matrix

**Table 18 — Hazard Coverage by Test Cases**

| Hazard | Description | Covering Tests |
| --- | --- | --- |
| H-01 | Unintended positive tractive effort | C-01–C-06, C-13, C-14, C-20, C-23–C-25, C-39–C-41, C-44, S-01, I-03, I-06–I-08, I-11, I-15–I-17, E-08, E-09 |
| H-02 | Unintended reverse tractive effort | S-02, S-14, C-13, I-13 |
| H-03 | Sudden loss of tractive effort | C-22, S-03, S-10, S-11, S-13, I-01, I-02, I-07, I-17 |
| H-03a | Loss of tractive effort during cornering | S-04 (dyno portion) |
| H-04 | Inability to produce regen | C-11, C-30 (indirect) |
| H-05 | Uncommanded regenerative braking | S-05, S-08, I-14 |
| H-06 | Excessive tractive effort | C-07, C-08, C-41, S-01, S-09, S-15 |
| H-07 | Over-temperature | C-09, C-10, C-35, C-45, S-06, S-07, E-04, E-07 |
| H-08 | Motor overspeed / encoder loss | C-11, C-28, C-29, C-30, S-15 |
| H-09 | HV isolation failure | C-50, I-04, E-07, E-12 |
| H-10 | DC link bus overvoltage | C-12, C-46, I-05, S-08, S-13 |
| H-11 | HV contactor welding | I-04, C-46 |
| H-12 | IGBT shoot-through | C-15, C-16, C-31–C-34, C-47–C-49 |
| H-13 | Failure to execute safe state | C-13, C-14, C-15, C-21–C-25, C-37, C-38, C-40, C-43, C-44, I-04, S-10, S-12, E-01–E-03, E-10, E-11 |
| H-14 | Corrupted/latched tractive effort | C-13, C-14, C-19, C-20, C-37, C-38, C-39, C-44 |
| H-15 | Incorrect tractive effort from software error | C-07, C-08, C-20, C-39, C-40, S-01, I-08, I-11 |
| H-16 | Gate driver fault not acted upon | C-15, C-16, C-17, C-26, C-27, C-31–C-34 |
| H-17 | PWM deadtime violation / stuck-on | C-15, C-16, C-49 |

## 10.8 Pass/Fail Criteria

All test cases in this plan use the following standardized pass/fail definitions. A test is **PASSED** only when all criteria are met. A test is **FAILED** if any criterion is not met.

**Table 19 — Test Pass/Fail Criteria Definitions**

| Criterion | Pass Definition | Fail Definition |
| --- | --- | --- |
| **Safe State Entry** | System enters defined safe state (SSO, zero tractive effort) within specified time limit. PWM outputs disabled. No tractive effort produced. | Safe state not entered within time limit. Tractive effort continues. PWM remains active. |
| **Detection Time** | Fault detected and logged within specified ms limit. DTC recorded with correct code. | Detection exceeds time limit. Incorrect DTC. No DTC recorded. |
| **Torque Ramp Rate** | Measured torque change rate ≤ specified Nm/s. Smooth, monotonic decay. No step changes. | Torque rate exceeds limit. Step change observed. Oscillation or non-monotonic behavior. |
| **HW Response** | Hardware protection (DESAT, breakpoint, watchdog) responds within specified us/ms limit, independent of software state. | HW response exceeds limit. Response depends on software execution. HW does not trigger. |
| **Latching** | Fault condition remains latched until key cycle or explicit reset. No automatic restart into operation. | System auto-recovers without reset. Fault condition clears spontaneously. Intermittent operation. |
| **Logging** | Fault recorded in non-volatile memory. DTC code matches fault type. Timestamp recorded. | Fault not logged. Incorrect DTC. Volatile-only storage (lost on power cycle). |
| **No Degradation** | System behavior outside the fault path is unaffected. Other safety functions remain operational. | Cascade failure: one fault causes unrelated safety function to fail. Side effects on non-fault paths. |

### 10.8.1 Overall Test Plan Assessment

The test plan as a whole is assessed against the following criteria:

**Table 20 — Overall Test Plan Assessment Criteria**

| Assessment | Definition |
| --- | --- |
| **ALL TESTS PASSED** | Every test case in Sections 10.3–10.5 achieves PASS on all criteria. All safety goals have at least one passing test. All FSRs have at least one passing test. The system is considered validated for its achievable ASIL level (ASIL D for SG-01, SG-13; ASIL B for SG-02, SG-07; ASIL C for SG-06, SG-12, SG-14, SG-15). |
| **PASSED WITH EXCEPTIONS** | All critical tests (covering P0 gaps) pass. Minor tests may fail with documented workarounds. Exceptions do not affect safety of the achievable ASIL claims. Mitigation plans documented for each exception. |
| **FAILED — NOT ROADWORTHY** | Any P0-gap test fails (throttle monitoring, safe state entry, watchdog response). System must not be operated on public roads until resolved. Hardware or software modifications required. |
| **INCONCLUSIVE** | Tests could not be executed due to equipment limitations or test environment constraints. Results insufficient for safety validation. Additional testing required before operation. |

## 10.9 Known Test Limitations

This test plan is honest about its limitations. The following conditions cannot be fully validated by the test cases described and require additional verification methods.

> **LIMIT-01: Vehicle-Level Dynamics Testing (H-03a Cornering)**
>
> **Limitation:** Test S-04 validates the torque ramp rate on a dyno but cannot replicate the full motorcycle-cornering-dynamics scenario. The interaction between tractive effort removal, tire friction envelope, chassis geometry, rider position, and lean angle can only be validated on a real motorcycle with an experienced test rider on a controlled track.
>
> **Mitigation:** (1) Dyno validates the controllable portion (ramp rate ≤200 Nm/s). (2) Track testing with graduated lean angles and known fault injection confirms no stand-up/run-wide behavior. (3) Rider feedback confirms controllability. This is essential before racetrack use.

> **LIMIT-02: Environmental Stress (Temperature, Vibration, EMI)**
>
> **Limitation:** Component and system tests are conducted at ambient temperature on a bench. The test plan does not include environmental stress screening (thermal chamber, vibration table, EMC chamber) that would be required for production qualification.
>
> **Mitigation:** (1) Railway-grade DC/DC (EC7BW-110S12) qualified to EN 50155 provides confidence in power supply robustness. (2) Gate drivers automotive-qualified (AEC-Q100) for temperature. (3) EMC and environmental testing recommended before production but not required for prototype safety validation.

> **LIMIT-03: Long-Term Aging and Wear**
>
> **Limitation:** Tests are conducted on relatively new hardware. Long-term effects (capacitor aging, contactor wear, solder fatigue, connector fretting) are not covered.
>
> **Mitigation:** (1) POST (FSR-16) catches degradation in critical protection circuits. (2) Regular inspection and maintenance intervals defined in service documentation. (3) BMS monitors cell degradation (outside VCU scope).

> **LIMIT-04: Single-Encoder Limitation (H-08)**
>
> **Limitation:** The system has a single encoder. There is no redundant rotor position sensor. All FSR-09 testing validates that the system *detects* encoder loss and enters safe state, but it cannot *prevent* loss of control during the detection window (up to 100 ms).
>
> **Mitigation:** (1) Keep detection window as short as possible (<50 ms target). (2) Sensorless fallback if implemented: bounded to ≤2 s and ≤30% tractive effort. (3) Document limitation: rider must be prepared for occasional safe-state entry due to encoder-related faults.

> **LIMIT-05: Common-Cause MCU Failure (with Shared 3.3V Rail)**
>
> **Limitation:** The main STM32H723 and NCV57100 gate driver logic side share the same 3.3V supply rail — there is no independent 3.3V regulator for the gate drivers. However, this shared rail provides a *passive* safe-state path: total loss of 3.3V → NCV57100 VDD lost → internal active pull-down forces all IGBT gates to 0V → SSO, with no software involvement (Path 3). The dual-MCU architecture mitigates common-cause failures: the coprocessor has its own oscillator, independent ADC channels, and independent power kill (GATE_DRIVE_PWR2_ENABLE). A common-cause affecting the main MCU (clock failure, supply collapse, latch-up) does not disable the coprocessor's independent monitoring and safe state actuation.
>
> **Mitigation:** (1) Six redundant SSO pathways (Path 2a/2b are redundant channels of one 1oo2 power-kill pathway): Path 1 = TIM1_BKIN hardware (<100 ns); Path 2a/2b = GATE_DRIVE_PWR1_ENABLE (main, with GATE_DRIVE_PWR1_FB) / GATE_DRIVE_PWR2_ENABLE (coprocessor, with GATE_DRIVE_PWR2_FB); Path 3 = shared 3.3 V passive pull-down; Path 4 = GATE_DRIVE_RESET (either MCU, <1 us); Path 5 = coprocessor watchdog → NRST (~100 ms); Path 6 = coprocessor independent fault trigger. (2) 1oo2 power kill: either GATE_DRIVE_PWR1_ENABLE or GATE_DRIVE_PWR2_ENABLE low achieves SSO. (3) Dual-MCU architecture enables ASIL D via ASIL B(D) + ASIL B(D) decomposition.

> **LIMIT-06: Gate Driver Non-ASIL Status**
>
> **Limitation:** The NCV57100 gate drivers are qualified to AEC-Q100, not ASIL. Internal DESAT, UVLO, and anti-shoot-through protections are not independently monitored. Tests C-15 through C-17 validate these protections function but do not provide ASIL credit.
>
> **Mitigation:** (1) No ASIL credit claimed for gate driver internal protections (Table 13 reflects ASIL C achievable for SG-12, SG-14). (2) Coprocessor PWM monitoring (future) enables ASIL C claim. (3) OR'd FLT input provides basic monitoring with known single-point limitation.

> **LIMIT-07: Software Test Library — Class B vs. Class D**
>
> **Limitation:** ST's publicly available X-CUBE-CLASSB library is certified to IEC 60730-1 Annex H (Class B) only. The ISO 26262-assessed Class D STL (X-CUBE-STL) requires a manufacturer NDA and is not available for open-source projects. The following ASIL process requirements are therefore not met:
>
> - No FMEDA / SPFM/LFM metrics derived for this specific STM32H723ZG hardware configuration
> - No ISO 26262 software tool qualification (compiler, static analysis, test tools)
> - No fault injection testing campaign with structural coverage evidence
> - No independent safety assessment or third-party audit
>
> This project implements ASIL decomposition as a **design and educational exercise only**. A production system targeting ISO 26262 compliance would require re-implementation under an ISO 26262-compliant development process, use of a certified STL (e.g., X-CUBE-STL under NDA from ST), or use of a pre-certified MCU + STL combination from an alternative supplier.
>
> **Mitigation:** (1) X-CUBE-CLASSB provides CPU register test, RAM March-C test, and flash CRC — adequate for POST but not ASIL-traceable. (2) All software safety mechanisms are independently tested via fault injection (99 tests). (3) Document this gap explicitly: achievable ASIL is limited by STL certification status.

> **LIMIT-08: No Dependent Failure Analysis (DFA)**
>
> **Limitation:** ISO 26262-9 requires a Dependent Failure Analysis for ASIL C and above. No DFA has been performed. This means common-cause and cascading failures between the six SSO paths (e.g., a PCB delamination affecting multiple traces; a single ground bounce affecting both break inputs) are not formally analyzed. Without DFA, the independence claim for ASIL B(D) decomposition cannot be fully substantiated.
>
> **Mitigation:** (1) Physical separation of TIM1_BKIN, GATE_DRIVE_PWR1_ENABLE, and GATE_DRIVE_PWR2_ENABLE routing on the PCB. (2) Independent ground references where possible. (3) The coprocessor's independent oscillator reduces common-cause with the main MCU; the +3.3 V rail shared by both MCUs is supervised by the TPS389006-Q1, which detects brownout and asserts the shared GATE_DRIVER_FAULT line monitored by both MCUs. (4) DFA to be performed before any formal ASIL D audit. (5) Documented as a blocker for formal ASIL D claim.

> **LIMIT-09: No EMI/EMC Pre-Compliance Assessment**
>
> **Limitation:** The fault injection test plan includes environmental EMI tests (E-08 radiated immunity, E-09 conducted immunity, E-10/E-11 ESD), but no pre-compliance EMC assessment or design margin analysis has been performed. The inverter will be installed in unknown vehicle electromagnetic environments (ignition noise, fuel pump transients, radio transmitters, cell phones). EMI-induced ADC noise, CAN errors, or false triggering of the TIM1 break input are real risks that have not been quantified.
>
> **Mitigation:** (1) Environmental tests E-08 and E-09 provide a baseline when equipment is available. (2) CISPR 25 pre-compliance testing recommended before any field deployment. (3) PCB design includes best-practice grounding, shielding, and filtering. (4) Documented as an open risk for aftermarket installation.

## 10.10 Environmental and Stress Tests

Environmental tests validate the hardware's ability to withstand the physical and electromagnetic conditions encountered in motorcycle operation. These tests require specialized equipment (thermal chamber, vibration table, EMC chamber) and are typically conducted as type tests on a representative unit rather than on every production unit. They are included here as a reference test plan for when equipment becomes available.

### 10.10.1 Vibration Tests

#### E-01: Random Vibration — Operating

**Objective:** Verify no mechanical failure or electrical intermittent under random vibration profile.

**Covered:** SG-13 (ASIL D), FSR-16, H-13 (mechanical failure mode)

**Setup:** Vibration table with random vibration capability. DUT mounted in production-intent mounting orientation. Accelerometers on DUT.

**Procedure:**

1. Apply random vibration profile per ISO 16750-3, Test VII — Passenger car, sprung masses: 10–1000 Hz, 5.0 g RMS, 8 hours per axis (X, Y, Z).
2. Operate system at 50% tractive effort during vibration (motor running on dyno, electronics vibrating).
3. Monitor for: intermittent connections, CAN errors, sensor dropouts, reset events, unexpected safe state entries.
4. After test: visual inspection for cracked solder joints, loose fasteners, connector back-out, component detachment.
5. Run functional test (C-01 through C-05) post-vibration and compare to baseline.

**Pass Criteria:** No intermittent faults during vibration. No CAN errors above baseline. No resets. Post-vibration functional test passes. No visible mechanical damage.

**Why this covers the requirement:** Motorcycle operation subjects electronics to severe vibration from engine/motor imbalance, road irregularities, and wheel imbalance. The random vibration profile simulates the broadband energy of real road conditions. Operating during vibration (rather than just powered) exercises electrical connections under mechanical stress.

#### E-02: Sinusoidal Vibration Sweep — Resonance Search

**Objective:** Identify mechanical resonances that could cause fatigue failure.

**Covered:** SG-13 (ASIL D), H-13 (mechanical)

**Setup:** Sine vibration table. DUT with accelerometers at critical locations (PCB center, large components, connector backshells).

**Procedure:**

1. Sweep 5–500 Hz at 1.0 g peak, 1 octave/minute.
2. Record transfer function (response/input) at each accelerometer.
3. Identify resonance frequencies (Q > 10 considered high risk).
4. Dwell at each resonance frequency for 10 minutes at 1.0 g.
5. Repeat for all three axes.

**Pass Criteria:** No resonances with Q > 20. No failures after dwell. PCB resonant frequency > 200 Hz (above primary road vibration energy). Post-test functional verification passes.

**Why this covers the requirement:** Resonance causes amplification of vibration at specific frequencies. A PCB resonating at 80 Hz (common wheel imbalance frequency on motorcycles) can experience 10–20x amplification, causing solder fatigue in hours instead of years. Identifying resonances allows redesign (stiffening, damping, relocation) before production.

#### E-03: Mechanical Shock

**Objective:** Verify survival of pothole/curb-impact shock events.

**Covered:** SG-13 (ASIL D), H-13 (mechanical)

**Setup:** Shock test table. DUT in production mounting.

**Procedure:**

1. Apply half-sine shock: 50 g, 11 ms duration, 3 shocks per direction (±X, ±Y, ±Z).
2. Monitor for electrical intermittent during shock (system powered but not operating).
3. Post-shock: visual inspection and functional test.

**Pass Criteria:** No electrical intermittent. No mechanical damage. Post-shock functional test passes.

**Why this covers the requirement:** Pothole impacts and curb strikes create severe shock loads. Large components (DC link capacitors, heatsinks) have the highest inertia and experience the greatest stress. Connector back-out is a common shock failure mode.

### 10.10.2 Thermal Tests

#### E-04: High Temperature Soak

**Objective:** Verify correct operation at maximum expected ambient temperature.

**Covered:** SG-07 (ASIL B), FSR-08, H-07

**Setup:** Thermal chamber. DUT operating on dyno.

**Procedure:**

1. Ramp ambient to +60°C (desert parking + solar load).
2. Stabilize for 1 hour (DUT at thermal equilibrium).
3. Operate at 100% load for 2 hours.
4. Monitor all temperatures. Verify no thermal protection false trips.
5. Verify all electronic functions operate correctly at temperature.

**Pass Criteria:** All functions operate correctly. IGBT temperature < T<sub>j,max</sub> − 20°C margin. Capacitor temperature < rated max − 10°C. No thermal derate required within 30 minutes.

**Why this covers the requirement:** Desert operation with solar load can raise under-seat ambient to +60°C or higher. All semiconductor devices must operate within their safe operating area. Thermal protection thresholds must not false-trip at high ambient but must still protect against genuine over-temperature.

#### E-05: Low Temperature Soak

**Objective:** Verify correct operation at minimum expected ambient temperature.

**Covered:** SG-13 (ASIL D), FSR-16, H-13

**Setup:** Thermal chamber at −20°C.

**Procedure:**

1. Soak DUT at −20°C for 4 hours (cold-soaked, not operating).
2. Attempt key-on and startup. Verify system starts successfully.
3. Verify pre-charge functions (capacitor ESR higher at cold, may affect time constant).
4. Operate at 50% load for 30 minutes.
5. Monitor for: slow switching (higher IGBT V<sub>CE(sat)</sub> at cold), sluggish gate driver response, stiff cables/connectors.

**Pass Criteria:** Successful cold start. Pre-charge completes within timeout. Stable operation. No false faults from cold-soaked components.

**Why this covers the requirement:** Cold temperature affects: capacitor ESR (electrolytic caps especially), battery capacity (reduced cranking power), cable flexibility (stiff HV cables stress connectors), semiconductor parameters (higher V<sub>th</sub>, slower switching). The system must start and operate in winter conditions.

#### E-06: Thermal Shock

**Objective:** Verify survival of rapid temperature transitions.

**Covered:** SG-13 (ASIL D), H-13 (mechanical)

**Setup:** Two-zone thermal shock chamber or liquid thermal shock bath.

**Procedure:**

1. 20 cycles: −20°C (30 min) → +60°C (30 min). Transition time < 1 minute.
2. DUT non-operating during shock cycles.
3. After 20 cycles: visual inspection and functional test.

**Pass Criteria:** No condensation inside enclosure. No cracked solder joints. No connector seal breach. Functional test passes.

**Why this covers the requirement:** Thermal shock causes rapid CTE mismatch stress. Large components (capacitors, transformers, power modules) experience the highest stress. Condensation from temperature crossing the dew point is a risk for HV electronics. This is a type test for production qualification.

#### E-07: High Humidity Operation

**Objective:** Verify no performance degradation or insulation breakdown in high humidity.

**Covered:** SG-09 (ASIL A), FSR-10, H-09

**Setup:** Humidity chamber. 85% RH at 40°C.

**Procedure:**

1. Operate DUT at 50% load for 48 hours at 85% RH, 40°C.
2. Monitor isolation resistance (HV to logic ground) every 4 hours.
3. Verify no isolation degradation.
4. Post-test: HiPot test at rated voltage to confirm insulation integrity.

**Pass Criteria:** Isolation resistance stable throughout test. HiPot passes post-test. No corrosion visible on PCB or connectors.

**Why this covers the requirement:** Humidity reduces surface resistivity of PCBs and insulation materials. In extreme cases, condensation can create conductive paths between HV and logic. The test validates enclosure sealing and conformal coating effectiveness.

### 10.10.3 Electromagnetic Compatibility Tests

#### E-08: Radiated EMI Immunity (ALSE)

**Objective:** Verify system operates correctly under radiated electromagnetic interference.

**Covered:** SG-01 (ASIL D), SG-13 (ASIL D), FSR-15, H-01, H-13, H-15

**Setup:** ALSE (Absorber-Lined Shielded Enclosure) or anechoic chamber. RF signal generator and amplifiers. Bicone and log-periodic antennas.

**Procedure:**

1. System operating at 50% tractive effort on dyno (inside chamber or with motor outside via feedthrough).
2. Apply radiated EMI: 20 MHz – 6 GHz, 100 V/m, AM 80% 1 kHz, all polarizations (horizontal, vertical).
3. Dwell at 1% frequency steps for minimum 2 seconds each.
4. Monitor for: resets, CAN errors, sensor dropouts, unexpected tractive effort changes, safe state entries.
5. Pay special attention to GSM/Cellular bands (700 MHz, 850 MHz, 1.9 GHz), WiFi (2.4 GHz, 5 GHz), and ISM bands.

**Pass Criteria:** No Class A deviations (loss of function, unintended tractive effort). Class B deviations (temporary performance degradation) permitted if self-recovering. No safe state entries unless justified.

**Why this covers the requirement:** Cellular phone towers, emergency services radio, and amateur radio can create strong RF fields. The STM32 and analog sensors are susceptible to RF interference that can cause: ADC reading errors (appearing as throttle or current faults), CPU resets, CAN bit errors. This is a critical test for safety validation.

#### E-09: Conducted EMI Immunity (BCI)

**Objective:** Verify immunity to conducted interference on harnesses.

**Covered:** SG-01 (ASIL D), SG-13 (ASIL D), FSR-15, H-01, H-15

**Setup:** BCI (Bulk Current Injection) probe and calibration fixture. DUT with full harness.

**Procedure:**

1. Inject RF current onto each harness line: 1 MHz – 400 MHz, up to 100 mA (per ISO 11452-4).
2. Test all harnesses: CAN bus, throttle wiring, encoder cable, HVIL, +12 V, gate driver connections.
3. Monitor for same deviations as E-08.

**Pass Criteria:** Same as E-08. No Class A deviations on any harness.

**Why this covers the requirement:** Conducted coupling is often more severe than radiated for low-frequency interference (AM radio, CB radio). Long harnesses act as antennas. The BCI test injects current directly onto the harness to simulate coupling from nearby transmitters.

#### E-10: ESD — Contact Discharge

**Objective:** Verify system survives electrostatic discharge to accessible surfaces.

**Covered:** SG-13 (ASIL D), FSR-15, H-13

**Setup:** ESD gun per ISO 10605. DUT fully assembled in production enclosure.

**Procedure:**

1. Apply contact discharge: ±4 kV, ±6 kV, ±8 kV to all accessible metal surfaces: connector shells, mounting bolts, heatsink, enclosure seams.
2. 10 discharges per point per voltage level.
3. System operating during test.
4. Monitor for resets, safe state entries, fault logging.

**Pass Criteria:** No Class A deviations at ±4 kV and ±6 kV. At ±8 kV: Class B permitted (self-recovering). No permanent damage.

**Why this covers the requirement:** ESD occurs during rider contact (mounting/dismounting), fueling/charging, and maintenance. The discharge can couple into signals through connector shells and enclosure seams. The test validates enclosure shielding and ESD protection devices.

#### E-11: ESD — Air Discharge

**Objective:** Verify system survives air discharge to insulated surfaces.

**Covered:** SG-13 (ASIL D), FSR-15, H-13

**Setup:** ESD gun. DUT in production enclosure.

**Procedure:**

1. Apply air discharge: ±4 kV, ±8 kV, ±15 kV to all accessible insulated surfaces: plastic covers, labels, connectors with plastic housings.
2. 10 discharges per point per voltage level.
3. Monitor for same deviations as E-10.

**Pass Criteria:** Same as E-10 with adjusted levels for air discharge.

**Why this covers the requirement:** Air discharge has a slower rise time but can find paths into the enclosure through seams and ventilation. The plastic enclosure is the primary defense; this test validates its effectiveness.

#### E-12: Water Ingress / IP Rating Validation

**Objective:** Verify enclosure sealing against water ingress.

**Covered:** SG-09 (ASIL A), FSR-10, H-09, H-11

**Setup:** Water spray rig per IEC 60529. DUT in production enclosure, mounted in production orientation.

**Procedure:**

1. **IPX4 (splashing water):** Spray from all directions for 10 minutes. Inspect interior for water entry.
2. **IPX5 (water jets):** 12.5 L/min nozzle, 3 m distance, all directions, 3 minutes. Inspect.
3. **IPX6 (powerful jets):** 100 L/min nozzle, 3 m distance, 3 minutes. Inspect.
4. After each test: insulation resistance measurement (HV to ground). HiPot test.
5. Post-water: operate system and verify no moisture-induced faults.

**Pass Criteria:** No water inside enclosure at target IP rating. Insulation resistance > 500 Ohm/V. HiPot passes. System operates correctly post-test. Target: minimum IP54 (IP65 preferred for under-seat motorcycle installation).

**Why this covers the requirement:** Motorcycle electronics are exposed to rain, wheel spray, and pressure washing. Water ingress causes: short circuits (HV to logic), corrosion (connector degradation), insulation failure (shock hazard). The target IP rating must be validated, not just designed.

## 10.11 Recommended Test Execution Order

The 99 defined tests (50 component, 19 system, 18 integration, 12 environmental) are organized into an execution sequence designed to detect simple, non-destructive faults early while deferring potentially destructive tests until basic functionality is confirmed. This approach follows the principle of **progressive validation**: each test group builds confidence before moving to the next, more aggressive group. A failure in an early group prevents advancing to later groups until resolved, avoiding expensive damage from compound failures.

### 10.11.1 Execution Sequence

**Table 21 — Test Execution Order and Justification**

| Order | Group | Tests | Risk Level | Justification |
| --- | --- | --- | --- | --- |
| **1** | Power-On Self-Test | C-16 (DESAT POST)<br>C-20 (Boot CRC) | None | Validate that POST and boot checks function before any HV is applied or PWM is enabled. These are purely diagnostic — no load, no HV, no movement. If POST fails, the unit must not be energized further. |
| **2** | Supply Integrity | C-21 (Brownout)<br>C-22 (Brownout recovery)<br>C-46 (Pre-charge)<br>C-41 (ADC ref drift) | Very Low | Validate power supply behavior before energizing the inverter. Brownout tests use no load. Pre-charge validates sequencing logic. ADC reference validates sensor measurement foundation. All can be done with reduced DC link voltage. |
| **3** | Gate Driver Integrity | C-47 (Miller clamp)<br>C-48 (Prop delay)<br>C-49 (Deadtime)<br>C-17 (UVLO)<br>C-26 (+15V short)<br>C-27 (−9V short) | Low | Gate driver tests validate the PWM output stage without connecting a motor. Miller clamp, propagation delay, and deadtime are bench tests with scope only. UVLO and supply short tests validate independent protection. No motor load = no movement risk. |
| **4** | Isolation & HV Safety | C-50 (Isolation)<br>I-04 (HVIL)<br>C-10 (IGBT temp sensors)<br>C-35 (DC link cap temp) | Low-Medium | HiPot validates isolation before HV is applied repeatedly. HVIL validates the safety interlock. Temperature sensor tests validate monitoring before thermal stress. DC link at operational voltage for first time. |
| **5** | Sensor Validation | C-01 to C-06 (Throttle)<br>C-07, C-08 (Current sensor)<br>C-09 (Temp sensors)<br>C-11 (Encoder)<br>C-12 (DC link voltage)<br>C-28, C-29, C-30 (Phase open)<br>C-42 (SPI ADC) | Medium | All sensors validated with motor connected but at low load. Throttle, current, temperature, encoder, voltage — the complete feedback chain. Phase open tests validate continuity. Low power first to catch sensor faults before full power. |
| **6** | Control Loop Validation | C-13 (WDT)<br>C-14 (Breakpoint)<br>C-37 (Clock failure)<br>C-38 (PLL unlock)<br>C-43 (CAN bus off)<br>C-44 (WDT starvation)<br>S-10 (Startup)<br>S-11 (Shutdown) | Medium | Validate the fundamental safety mechanisms with motor running at low-to-medium load. Watchdog, breakpoint, clock, and PLL tests verify safe-state paths. Startup/shutdown sequencing validated. CAN fault handling at operational load. |
| **7** | Software Integrity | C-18, C-19 (ECC RAM)<br>C-39 (Flash bit rot)<br>C-40 (GPIO stuck-at)<br>C-15, C-16 (DESAT + self-test)<br>I-08 (CAN fuzzing)<br>I-09 (CAN bus load)<br>I-10 (Bus off recovery)<br>I-11 (Invalid frames) | Medium | Software fault injection with motor at medium load. ECC RAM, flash corruption, GPIO faults, and CAN robustness. DESAT self-test re-validated after initial power-on. These tests exercise the firmware under realistic conditions. |
| **8** | Integration & Coordination | I-01 to I-03 (CAN heartbeat)<br>I-06 (Kickstand)<br>I-12 to I-15 (Display, charger, ABS, BMS)<br>I-16 (Multi-node fault)<br>I-17 (CAN wiring)<br>I-18 (Wake/sleep)<br>S-12 (Key cycle stress) | Medium | Full system integration tests with all external nodes. Coordination between subsystems validated. Key-cycle stress test (1000 cycles) as endurance screen. All previous tests passed before this group. |
| **9** | Performance Validation | S-06 (Full load thermal camera)<br>S-07 (Thermal cycling)<br>S-08 (Max regen)<br>S-09 (Field weakening)<br>S-13 (Power dip ride-through)<br>S-14 (Reverse)<br>S-15 (Overspeed)<br>C-36 (Bearing current) | Medium-High | First full-power tests. Thermal camera survey identifies hotspots before extended run. Max regen validates overvoltage protection at worst case. Field weakening validates control stability at speed extremes. Power dip validates DC link capacitor sizing. Bearing current validates motor protection. |
| **10** | Fault Response Validation | S-01 to S-05 (System fault scenarios)<br>C-45 (Thermal runaway)<br>C-23 to C-25 (Power rail shorts)<br>C-31 to C-34 (Phase shorts)<br>I-05 (DC link OV)<br>I-07 (Multi-CAN loss) | High | Full-power fault injection. Unintended tractive effort, loss of tractive effort, regen faults at realistic power levels. Phase-to-phase and phase-to-DC shorts at reduced voltage. Power rail shorts validate supply fault behavior. Requires all previous groups passed. |
| **11** | Environmental Stress | E-01 to E-03 (Vibration, shock)<br>E-04 to E-07 (Thermal)<br>E-08, E-09 (EMI)<br>E-10, E-11 (ESD)<br>E-12 (Water ingress) | Variable | Environmental tests are type tests (one unit, not every unit). Run on a unit that has passed all functional tests. Vibration and thermal can reveal mechanical weaknesses. EMI and ESD validate electromagnetic robustness. Water ingress validates sealing. Some tests (phase shorts, EMI) can damage the unit — these are final. |

### 10.11.2 Stopping Criteria Between Groups

Advancement from one test group to the next requires **all** tests in the current group to pass. The following stopping criteria apply:

**Table 22 — Inter-Group Stopping Criteria**

| Scenario | Required Action | Before Advancing |
| --- | --- | --- |
| Any test in Group 1–3 fails | Stop. Do not apply HV. Debug hardware or software. Fix root cause. Re-run failed test + all preceding tests. | All tests in current group pass. |
| Any test in Group 4–5 fails | Stop. HV may be applied but do not connect motor or load. Debug sensor, isolation, or supply issue. Fix and re-run group. | All tests in current and all prior groups pass. |
| Any test in Group 6–8 fails | Stop. Motor is connected and spinning. Safe state may be untrusted. Debug safety mechanism. Fix and re-run from Group 5. | All tests in current and all prior groups pass. |
| Any test in Group 9 fails | Stop. Full power has been applied. Thermal or electrical damage may have occurred. Inspect hardware. Fix and re-run from Group 5. | All tests pass. Visual inspection confirms no damage. |
| Any test in Group 10 fails | Stop. Destructive fault testing may have damaged unit. Inspect thoroughly. If unit damaged, switch to fresh unit and re-run from Group 8. | All tests pass. Full functional verification (Groups 5–8) re-run and pass on same unit. |
| Any environmental test fails | Document failure. Environmental failures are type-test issues, not unit-test blockers. Fix design, re-test on new unit. | All prior functional groups pass on test unit. Environmental failure documented. |

### 10.11.3 Hardware Damage Risk Classification

**Table 23 — Per-Test Hardware Damage Risk**

| Risk | Tests | Potential Damage | Mitigation |
| --- | --- | --- | --- |
| **None** | C-16, C-20, C-21, C-41, C-46, C-47, C-48, C-49, I-08, I-11 | No electrical or mechanical stress. | Standard bench equipment. |
| **Very Low** | C-22, C-17, C-37, C-38, C-43, C-50, I-09, I-10 | Possible MCU reset. No power stage stress. | Current-limited supplies. No motor load. |
| **Low** | C-26, C-27, C-35, C-42, C-44, I-04, I-12, I-17 | Gate driver supply stress. Possible fuse blow. | Current-limited supplies. Fuses rated for test. |
| **Medium** | C-01–C-15, C-18–C-19, C-28–C-30, C-36, C-39–C-40, S-01–S-05, S-10–S-14, I-01–I-03, I-05–I-07, I-13–I-16, I-18, E-10–E-11 | Motor movement. Low-level power applied. Thermal stress possible. | Dyno with mechanical guard. Low initial load. Thermal monitoring. |
| **Medium-High** | S-06–S-09, S-15, C-09–C-10, C-12, C-45 | Full power thermal stress. Possible overtemperature if protection fails. | Thermal camera monitoring. Manual abort button. Pre-set temperature abort threshold. |
| **High** | C-23–C-25, C-31–C-34, C-07–C-08, S-13, I-05, E-01–E-09, E-12 | Power stage damage from shorts. Thermal damage from full load. Mechanical damage from vibration. | Reduced DC link for shorts (50 V). Fused test fixtures. Remote operation. Vibration fixture rated for DUT mass. Fire suppression for full-power tests. |
| **Very High** | C-31–C-34 (full voltage) | IGBT explosion. DC link capacitor rupture. Arc flash. Fire. | **Never run at full DC link voltage.** Maximum 50 V for phase short tests. Blast shield. Remote operation only. Fire extinguisher present. No personnel in room during test. |

> **Safety Warning for Phase Short Tests (C-31 through C-34)**
>
> Phase-to-phase and phase-to-DC short circuit tests are **inherently destructive if protection fails**. Even at reduced voltage, fault currents of 1000+ A are possible. The test procedure must include: (1) blast shield or enclosure around DUT, (2) remote-controlled contactor for short application (no manual connection), (3) current-limited DC supply or fuse rated below IGBT withstand, (4) fire suppression equipment, (5) no personnel in the test area during fault application, (6) video recording for post-test analysis, (7) pre-test verification that DESAT self-test (C-16) passed on the same day. If DESAT self-test has not been run within 24 hours, do not proceed with short-circuit tests.

# 11. Implementation Roadmap

This section provides a practical phased roadmap for implementing the safety requirements and closing the gaps identified in Section 9. The roadmap is organized by priority and effort level.

## 11.1 Phase 1: Safety-Critical Foundation (Immediate)

**Table 24 — Phase 1 — Safety-Critical Foundation**

| Gap | Action | Effort | Validation |
| --- | --- | --- | --- |
| GAP-HW-01 | **CLOSED** — Dual-MCU integrated monitoring detects overcurrent within 100 ms. LM397 comparators retained as non-safety redundant layer. No separate HW OCP required for safety case. | N/A | C-07, C-08, S-09, S-15: verify overcurrent detection and SSO response under dual-MCU monitoring |
| GAP-SW-01 | Implement boot CRC-32 using STM32 CRC peripheral. Check safety-critical code + calibration data before PWM enable. | Low | C-20: verify corrupted flash prevents boot |
| GAP-SW-02 | Implement reverse interlock: speed >100 rpm forward → negative tractive effort clamped to zero. Reverse only when stationary AND selected. | Low | S-02: verify reverse rejected at speed |
| GAP-SW-03 | Define sensorless fallback policy: immediate SSO on encoder loss. Document decision. If sensorless implemented: bound to ≤2 s, ≤30% tractive effort. | Low | C-11: verify encoder loss → SSO <100 ms |

## 11.2 Phase 2: Graceful Degradation (Short Term)

**Table 25 — Phase 2 — Graceful Degradation**

| Gap | Action | Effort | Validation |
| --- | --- | --- | --- |
| GAP-HW-02 | Implement tractive effort ramp-down at ≤200 Nm/s before SSO. Active torque decay on fault detection. Exception: <50 ms safety timing permits direct SSO. | Medium | S-03, S-04: verify ramp rate on dyno |
| FSR-06 | Implement uncommanded regenerative braking monitor. Compare commanded vs. actual regen. >10 Nm for >200 ms → safe state. | Medium | S-05: verify detection and response |
| FSR-02 | Implement tractive effort command plausibility: commanded current reference vs. measured phase currents. >20% for >100 ms → safe state. | Medium | C-07, C-08: verify plausibility catches sensor faults |
| FSR-17 | Implement CAN heartbeat timeout handling with safe-state defaults. IO board loss → brake=pressed, kickstand=down. BMS loss → tractive effort zero. | Medium | I-01, I-02: verify timeout and defaults |
| FSR-08 | Implement 2-out-of-3 IGBT temperature voter. Progressive derate at warning. SSO at critical. Thresholds in ECC memory. | Medium | C-09, C-10: verify voter behavior |

## 11.3 Phase 3: Test Execution and Validation

**Table 26 — Phase 3 — Test Execution and Validation**

| Activity | Effort | Description |
| --- | --- | --- |
| Execute component tests (C-01 to C-20) | High | Full component-level test campaign on bench setup. Document all results. Fix any failures before proceeding. |
| Execute system tests (S-01 to S-05) | High | Dyno-based system tests. Measure ramp rates, detection times, and response characteristics. Compare against requirements. |
| Execute integration tests (I-01 to I-07) | Medium | CAN-based integration tests with simulators. Validate timeout behavior, safe defaults, and CAN-loss response. |
| Track testing (LIMIT-01) | High | Controlled track testing with experienced rider. Graduated lean angle tests with known fault injection. Rider controllability assessment. |
| Test report and safety case | Medium | Compile test results into safety case document. Justify achievable ASIL claims based on evidence. Document all known limitations. |

## 11.4 Coprocessor Integration Validation

The STM32G474RCTx safety coprocessor is **part of the current hardware design** (ControlBoard.kicad_sch, Rev A). The following table maps coprocessor capabilities to safety goals:

**Table 27 — Coprocessor Capability to Safety Goal Mapping**

| Feature | Description | Impact |
| --- | --- | --- |
| Independent throttle monitoring | Coprocessor reads throttle pots (THROTTLE_A, THROTTLE_B) via independent ADC channels. Discrepancy >5% → GATE_DRIVE_PWR2_ENABLE low + GATE_DRIVE_RESET → SSO. Eliminates common-cause with main MCU. | SG-01 → ASIL D |
| Independent current monitoring | Coprocessor samples all 4 current sense signals (PH_U/V/W + DC link) + REF via independent ADC. Cross-checks against main MCU torque command. Overcurrent → SSO within 100 ms. | SG-06, SG-07 → ASIL target |
| Challenge-response watchdog | Coprocessor issues challenges via inter-MCU UART; main STM32 must respond within window. Failure → coprocessor asserts NRST → system reset → SSO. Not subject to on-chip common-cause. | SG-13 → ASIL D |
| PWM integrity monitoring | Coprocessor monitors all 6 PWM output pairs (PH_U/V/W_HIGH/LOW) for deadtime violations, stuck-on, stuck-off, and frequency anomalies. Independent of main STM32. | SG-12, SG-14, SG-15 → ASIL C |
| 1oo2 gate drive power kill | GATE_DRIVE_PWR2_ENABLE (coprocessor) in logical-OR with GATE_DRIVE_PWR1_ENABLE (main). Either MCU deasserting its enable kills all six gate drive supplies. Independent feedback: GATE_DRIVE_PWR1_FB + GATE_DRIVE_PWR2_FB. | SG-13 → ASIL D |
| Independent CAN snooping | FDCAN2 + FDCAN3 monitor both CAN buses. Coprocessor cross-checks torque commands, heartbeats, and fault flags against main MCU actions. CAN anomaly → SSO. | SG-01, SG-04, SG-05 → ASIL target |
| Independent temperature monitoring | All 3 heatsink temps + motor temp monitored via independent ADC. 2oo3 voting cross-check with 100 °C hard cap. Thermal runaway → SSO. | SG-07 → ASIL target |
| FRAM logging | CY15B102Q-SXET 256 KB FRAM (attached to the main MCU via SPI) stores fault logs, configuration, hour meter, odometer. Hardware WP pin protects critical data. | SG-03 → ASIL C |

The dual-MCU architecture makes **ASIL D achievable for SG-01 and SG-13 via ASIL B(D) + ASIL B(D) decomposition**. All other safety goals achieve their target ASIL. No hazards remain below target. The coprocessor firmware must be validated alongside the main MCU firmware — see test cases C-14 (coprocessor SSO), S-16 through S-19 (modulation transitions), and I-16 (multi-node fault with coprocessor participation).

# References

**Table 28 — Referenced Standards and Documents**

| Reference | Title / Description |
| --- | --- |
| ISO 26262-1:2018 | *Road vehicles — Functional safety — Part 1: Vocabulary*. International Organization for Standardization. |
| ISO 26262-3:2018 | *Road vehicles — Functional safety — Part 3: Concept phase*. Defines HARA methodology, ASIL assignment, and safety goal derivation. |
| ISO 26262-5:2018 | *Road vehicles — Functional safety — Part 5: Product development at the hardware level*. Hardware architectural metrics and safety mechanisms. |
| ISO 26262-9:2018 | *Road vehicles — Functional safety — Part 9: Automotive Safety Integrity Level (ASIL)-oriented and safety-oriented analyses*. ASIL decomposition and dependent failure analysis. |
| EN 50155:2021 | *Railway applications — Electronic equipment used on rolling stock*. Qualification standard for onboard power supplies and electronic equipment in railway applications. |
| IEC 61508 | *Functional safety of electrical/electronic/programmable electronic safety-related systems*. General functional safety standard; SIL terminology reference. |
| AEC-Q100 | *Failure Mechanism Based Stress Test Qualification for Integrated Circuits*. Automotive Electronics Council. Qualification standard for the NCV57100 gate driver. |
| STM32H723ZG | *STM32H723/733 — Arm Cortex-M7 MCU Reference Manual (RM0433)*. STMicroelectronics. Main processor: 550 MHz, ECC RAM, dual FDCAN, HRTIM. |
| STM32G474RCTx | *STM32G474/484 — Arm Cortex-M4 MCU Reference Manual (RM0440)*. STMicroelectronics. Safety coprocessor: 170 MHz, FPU, 3x FDCAN, advanced motor-control timers, CORDIC. |
| CY15B102Q-SXET | *2-Mbit (256K x 8) Serial (SPI) F-RAM*. Infineon/Cypress. Non-volatile storage with unlimited write endurance, hardware WP pin. |
| NCV57100 | *Isolated IGBT Gate Driver with Desaturation Protection* — ON Semiconductor / onsemi Datasheet. DESAT, UVLO, complementary inputs, active Miller clamp, soft turn-off specifications. |
| EC7BW-110S12 | *EC7BW-110 Series — 20 W Isolated DC-DC Converter*. Cincon / Railway Grade. EN 50155 qualified onboard power supply. |
| Cossalter | *Motorcycle Dynamics (2nd ed.)*, Vittore Cossalter. Lulu Press, 2006. Motorcycle handling dynamics, tire friction envelopes, and lean angle mechanics reference. |
| NHTSA | *National Highway Traffic Safety Administration — Motorcycle Safety*. https://www.nhtsa.gov/motorcycles. Statistics and research on motorcycle accident causation. |

## Document History

**Table 29 — Revision History**

| Version | Date | Changes |
| --- | --- | --- |
| 1.0 | 2026-06-13 | Initial unified HARA document. Combined previous separate documents. Added railway terminology, mitigation strategy table, expanded cornering test rationale, fault injection test plan, implementation roadmap. |
| 2.0 | 2026-06-13 | Major expansion. Added 30 new component tests (brownout, power rail shorts, phase disconnect/short, bearing current/Class Y caps, DC link cap temperature, clock failure, PLL unlock, flash bit rot, GPIO stuck-at, ADC drift, SPI faults, CAN bus off, watchdog starvation, thermal runaway, pre-charge, Miller clamp, propagation delay, deadtime, isolation). Added 14 new system tests (10 original + 4 modulation transition) (full load thermal camera, thermal cycling, max regen, field weakening, startup/shutdown, key cycle stress, power dip ride-through, reverse, overspeed). Added 11 new integration tests (CAN fuzzing, bus load, bus off recovery, invalid frames, display, charger, ABS, BMS faults, multi-node, wiring faults, wake/sleep). Added 12 environmental tests (vibration random/sine/shock, thermal soak/high/low/shock, humidity, EMI radiated/conducted, ESD contact/air, water ingress). Added test execution order with progressive validation and hardware damage risk classification. Total: 75 tests across 4 categories. |
| 3.0 | 2026-06-14 | Added LIMIT-07 (STL Class B only), LIMIT-08 (no DFA), LIMIT-09 (no EMI/EMC pre-compliance). Added CORDIC subsection. Added two ASIL evaluations (with/without coprocessor). Clarified encoder and CAN nodes out of scope. Added gate driver FLT verification on PMIC EN power cut. Added complete state machine transition table. Added FOC tested on bench note. Added 17% statistic note. |
| 3.1 | 2026-06-14 | Added 4 system-level tests (S-16 to S-19) for multi-modulation scheme transitions. Total: 75 tests. |
| 4.0 | 2026-07-08 | Major architecture update: Dual-MCU is now the primary architecture. STM32G474RCTx coprocessor implemented. Six SSO pathways. GAP-HW-01 closed — dual-MCU integrated monitoring sufficient, HW OCP not required. 1oo2 gate drive power kill with independent feedback. ASIL D via ASIL B(D) decomposition. |
| 4.1 | 2026-07-13 | Editorial consistency pass: fault-injection test counts corrected to 99 (50 component / 19 system / 18 integration / 12 environmental); NCV57100 gate-driver part update; DRIVER_RESET turn-off mechanism corrected; bleeder assumption removed; TPS389006-Q1 rail supervisor integrated; stale ASIL and test-reference errors fixed; shared 3.3 V rail and gap-analysis staleness corrected; temperature voting updated from 1oo3 to 2oo3. |
