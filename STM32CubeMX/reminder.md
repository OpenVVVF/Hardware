# Development Reminders

This file tracks known gaps and deferred work that should be addressed before high-power / FOC operation.

## Safety

- **Watchdog (IWDG/WWDG):** Not enabled yet. `HAL_IWDG_MODULE_ENABLED` and `HAL_WWDG_MODULE_ENABLED` are commented out in `Inc/stm32h7xx_hal_conf.h`. Implement and enable before high-power testing or unattended operation.
- **Software overcurrent fault integration:** `PhaseCurrentADC` samples phase currents every PWM cycle, but the software overcurrent threshold defaults to 1000 A (effectively disabled). Wire the current-limit check into `FaultManager` as a latched Critical fault and add a sensible default threshold + shell command to adjust it.

## Control / Calibration

- **Refactor `OpenLoopController::rampModulation`:** Currently uses 20 sequential `HAL_Delay(5)` calls inside a 100 ms blocking ramp while ISRs continue. A critical fault during the ramp cannot abort until the ramp completes. Make the ramp non-blocking and driven from the main loop or a periodic tick so `FaultManager::executeSafetyActions()` can shut down immediately.

## Flash / Debug Scripts

- **Out-of-date flash scripts:** The following scripts are stale and should not be used without updating:
  - `build_flash.sh`
  - `build_flash_uart_manual.sh`
  - `flash_uart_manual.py`
- **Working flash path:** Use `build_flash_uart.sh` + `flash_uart.py` for MCP2221A UART bootloader flashing. If the InverterClientImGui app is running, `build_flash_uart.sh` automatically hands the binary to the client's HTTP flash API (`http://localhost:18080/flash`, override with `INVERTER_CLIENT_FLASH_URL`) so the client can stay open; otherwise it flashes directly.
- **Post-bootloader drain is mandatory:** the app's telemetry spam leaves garbage buffered in the MCP2221A/host tty that drowns the ROM bootloader's sync ACK ("Activating device: KO"). Both `flash_uart.py` and the client's `FirmwareUpdater` drain the port once before entering the bootloader and once after, right before `STM32_Programmer_CLI` runs. Do not remove the second drain.
- **Keep:** `setup_mcp2221a.py` is still used for one-time MCP2221A GPIO configuration.

## Verified / Accepted Items

- DC-link voltage scaling (default 1516.0) has been measured and is correct.
- TIM1 timer clock assumption (`TIM1_CLOCK_HZ = 275000000UL`) and 10 kHz switching frequency are acceptable as-is.

## F-RAM Storage (FramStore)

- **Generic record store:** `FramStore` (`Inc/Src Inverter/Drivers/Storage/`) keeps fixed 256-byte slots addressed by node id (`addr = node_id * 256`), each with magic/node/version/length/CRC32. Node 1 = motor config (`MotorConfigStore`); add new nodes for other persistent state. The on-time logger stays at address 0 (node ids start at 1).
- **Motor config:** `motorcfg dump/save/load/clear/set/type`. Boot auto-loads; a successful `motorcal` auto-saves. `MotorType` enum is reserved for future machine families (only `pmsm_ipm` has control support). Schema v2 also stores the learned encoder sin/cos amplitude bounds: without them, a fresh boot normalizes the encoder angle against the wide hardcoded caps, and the resulting 2nd-harmonic angle error can detent-lock the rotor under FOC ("hiccup then no turn"). v1 records still load (bounds read as unset).
- **Cold motorcal quirk:** if the encoder bounds have never been learned (erased/new FRAM), they become valid mid-way through the OFFSET rotation and the angle normalization switches there, which can leave the rotor unsettled for the resistance phase (seen once as a spurious 213 A overcurrent FAIL). Retry motorcal — the second attempt runs with valid bounds from the start.
- **SPI4 runs at /32 (~4.3 MHz), not the CubeMX default /4 (34 MHz):** at 34 MHz the polling HAL receive was overrun by the 10 kHz ADC ISR, corrupting reads. `CY15B102Q_Read` now also masks interrupts for the transfer window — do not remove that, and do not raise the prescaler back without testing `motorcfg raw` (20-read hammer with error counter) under load. If CubeMX regenerates `spi.c`, re-apply the /32 prescaler (`.ioc` was updated to match).

## Switching Frequency / Modulation (runtime variable)

- **The switching frequency and modulation scheme are user-defined runtime settings, not constants.** Do not bake rate assumptions into control or calibration code. The current frequency is always available live via `PWM_GetFrequency()` / `PWM_GetUpdateFrequency()`.
- `FocControlManager` recomputes the control-loop `dt` whenever the update frequency changes (checked in `update()`); keep it that way.
- Calibration routines may pin specific values for their measurement (e.g. `InductanceCalibrator::CAL_SWITCHING_HZ`) but MUST save and restore the user's setting (see `restoreSwitchingFrequency()` and its use on every exit path).
- The encoder/RPM path derives its sample rate from the live TIM2 configuration (`EncoderADC::m_sample_hz`), not a literal.

## Bus-Voltage Robustness (60 V lessons)

- **Open-loop phases need a continuous current clamp, not ramp pauses.** `OpenLoopController::updateCurrentClamp()` throttles the applied modulation whenever phase current exceeds `rclimit` (default 250 A, was 600 A) and recovers slowly; `CurrentLimitedRamp` throttles the same way (floored at 0.5x target so a spike cannot kill rotation). At 60 V the old pause-only logic either tripped a 1000 A single-sample OC fault or aborted the offset cal.
- **Software overcurrent is deglitched:** 3 consecutive samples over threshold (default 500 A), because one-sample trips false-fault on 60 V switching transients.
- **Rotation duty = breakaway duty (factor 1.0), not x1.3.** Measured at 60 V: min-follow mod ~0.05-0.065 (~40-80 A); x1.3 drew ~460 A; factor 1.0 keeps rotation current roughly voltage-independent (breakaway mod scales inversely with voltage). Pole counting tolerates weaker rotation; the offset rotation needs near-full field tracking - check enc offset matches the known-good ~68.7 deg and enc_cycles ~1.00 after any change here.
- Flux-linkage fit is much cleaner at 60 V (bigger EMF-to-VSI-offset ratio; ceiling ~666 RPM). psi_m = 0.071 Wb at 60 V agrees with the 30 V fits (0.070-0.073), and V_off fits smaller (~1.0 V vs ~1.8 V).

## Flux Linkage Calibration (back-EMF ramp)

- **Method:** `FluxLinkageCalibrator` (`fluxcal start [max_iq]`, default 20 A) ramps iq 0 -> max over 12 s under FOC (id=0) and samples psi_m = (vq - R*iq)/omega_e every 50 ms, so each point of the speed sweep is a measurement. Stops at the PI voltage ceiling (near the ceiling the PI output no longer equals applied voltage and you measure the limit constant, not the motor).
- **Fit:** least-squares `vq - R*iq = omega_e*psi_m + V_off` over all samples. The V_off (VSI/deadtime offset) term is essential - without it low-speed samples inflate psi_m by up to 60%. Fitted V_off (1.6-1.9 V) matches the resistance-cal intercept (~1.5 V), which is a good self-consistency check.
- **Caveats:** estimate quality depends on high-speed coverage (the fit degrades when the ceiling is hit early); expect ~+/-10% run-to-run. Higher bus voltage widens the EMF-to-offset ratio and should give cleaner fits. Median-of-samples is printed only as an (offset-biased) cross-check.
- Measured on the bench motor (30 V bus): psi_m ~= 0.07-0.08 Wb. Stored in `MotorCalibration.flux_linkage_wb` / FRAM `flux_linkage_wb` and feeds the FOC feedforward via `buildMotorParametersFromCalibration`.
- **Known fragility (pre-existing):** pole cal miscounted once during this work (12 instead of 10 poles; downstream offset/inductance/flux all skewed and got auto-saved). A rerun recovered. If motorcal results look off, check `motorcal status` poles before trusting the FRAM profile - a plausibility recheck before auto-save is future work.
- An EKF online estimator (Liu & Hameyer, review ref [92]) remains a possible future addition for tracking parameter drift (magnet temperature) during operation; it is not needed for one-shot commissioning and is much more tuning-sensitive.

## Ld/Lq Calibration (biased-AC injection)

- **Method:** `InductanceCalibrator` (`indcal start [max_a] [ac_a] [freq_hz]`, defaults 30 A / 3 A / 150 Hz) implements the DC-biased AC standstill test from Rafaq & Jung, IEEE TII 2020, Section III-B: sweep a DC bias current, superimpose a small AC current, and take the differential inductance from the Goertzel-extracted AC voltage/current ratio at the injection frequency (L = sqrt(Z²−R²)/ω, R from motorcal). Runs in closed-loop FOC current control; the per-cycle hook is `FocControlManager::setSampleCallback`.
- **Ld is measured on the d axis, Lq on the q axis — but NOT with a locked rotor.** In the closed-loop encoder frame, constant id makes no torque (Ld points need no equilibrium) and constant iq makes constant torque (no equilibrium exists). So the rotor spins freely during Lq points; the rotation back-EMF lives at the (much lower) electrical rotation frequency and does not contaminate the injection-frequency bin. Do NOT try to "hold" the rotor with a d-axis current in the FOC frame — there is no alignment torque there, the rotor just runs away (learned the hard way).
- Small-signal distortion (VSI deadtime ~24 mV at 30 V bus) swamps weak responses: the calibrator auto-retries with doubled AC amplitude (up to 10 A) and skips points that stay below 1.5 A actual AC. Bias ladders start at 15% of max; Lq ladder is scaled to 0.4× (rotor acceleration sanity). Headlines (ld_henry/lq_henry) come from the SECOND bias point (better SNR than the first).
- Integrated as the INDUCTANCE stage of `motorcal` (auto-saved to FRAM with the curve, schema v4); results land in `MotorCalibration.ld_henry/lq_henry` and feed `buildMotorParametersFromCalibration`.
- Bench reference (LCR meter, line-line): ~120 µH min / ~210 µH max depending on rotor position → ~60/105 µH per-phase. Measured curves: Ld ≈ 60-75 µH, Lq ≈ 110-140 µH. First-point values run high; mid-ladder points are the trustworthy ones.

## Current-Sensor Offset Calibration Sequence (DO NOT BREAK)

The phase-current zero offset is **very sensitive to the electrical/load state** of the gate-driver / isolated-sensor rails. A manual `cal` is accurate because it runs long after the system has reached its operating state. For **startup** calibration to match it, the capture must happen:

1. **After the gate-driver power rail is enabled** — the isolated current sensors and their references shift zero point when this rail comes up.
2. **After `PWM_Start()` has enabled the TIM1 PWM outputs** — even though `GATE_DRIVER_RESET` is asserted (so the IGBTs cannot switch), the gate-driver inputs and isolated supplies see the same operating load as during normal idle operation. Calibrating before the PWM outputs are started produces a different, less accurate zero offset.

The correct sequence is therefore:

```text
InverterMain::init()
  └─ enable GATE_DRIVER_POWER_ENABLE, wait 500 ms
  └─ CurrentSensorTest_Init()           // start PhaseCurrentADC, do a quick initial offset
  └─ OpenLoopController::init()
        └─ set PWM 10 kHz, park 50 %
        └─ assert GATE_DRIVER_RESET
        └─ enable GATE_DRIVER_POWER_ENABLE (redundant but harmless)
        └─ HAL_Delay(50)
        └─ PWM_ClearFault();
        └─ PWM_Start();                 // IMPORTANT: outputs enabled while reset asserted
        └─ HAL_Delay(100);
        └─ phaseCurrentADC().recalibrateOffsets();  // authoritative startup offset
```

**History:** The command-manager refactor (`f367224`) moved the final offset capture out of `OpenLoopController::init()` and into `PhaseCurrentADC::start()`, which runs **before** `PWM_Start()`. That made startup calibration consistently offset from a manual `cal`. It was fixed by restoring the final `recalibrateOffsets()` call after `PWM_Start()` in `OpenLoopController::init()`.

**Files:** `Src/Inverter/Control/OpenLoopController.cpp`, `Src/Inverter/Drivers/Sensors/PhaseCurrentADC.cpp`, `Src/Inverter/InverterMain.cpp`.

## Encoder Offset Calibration Notes

- See `docs/EncoderOffsetCalibration.md` for a detailed write-up of a tracker-reference bug that caused `encoffset` to return ~58° instead of the true ~13°, and the fix (use raw absolute encoder angle + field-angle unwrapping, not the movement tracker).
