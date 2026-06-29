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
