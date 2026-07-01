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
- **Working flash path:** Use `build_flash_uart.sh` + `flash_uart.py` for MCP2221A UART bootloader flashing.
- **Keep:** `setup_mcp2221a.py` is still used for one-time MCP2221A GPIO configuration.

## Verified / Accepted Items

- DC-link voltage scaling (default 1516.0) has been measured and is correct.
- TIM1 timer clock assumption (`TIM1_CLOCK_HZ = 275000000UL`) and 10 kHz switching frequency are acceptable as-is.

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
