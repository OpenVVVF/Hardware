#include "Inverter/InverterMain.h"
#include "Inverter/Telemetry.h"
#include "Inverter/Control/FaultManager.h"
#include "Inverter/Control/OpenLoopController.h"
#include "Inverter/Control/FocControlManager.h"
#include "Inverter/Control/CommandShell.h"
#include "Inverter/Drivers/Sensors/CurrentSensorTest.h"
#include "Inverter/Drivers/Sensors/DcLinkVoltageSensor.h"
#include "Inverter/Drivers/Sensors/EncoderADC.h"
#include "Inverter/Drivers/CAN/FdcanFault.h"
#include "Inverter/Drivers/Logging/SupplyMonitor.h"
#include "Inverter/Calibration/PoleCalibrator.h"
#include "Inverter/Calibration/EncoderOffsetCalibrator.h"
#include "Inverter/Calibration/ResistanceCalibrator.h"
#include "Inverter/Calibration/AutoCalibrationCoordinator.h"

#include "main.h"
#include "spi.h"
#include "cy15b102q_driver.h"
#include "ontime_logger.h"

namespace InverterMain {

static CY15B102Q_HandleTypeDef g_fram = {
    .hspi      = &hspi4,
    .cs_port   = FRAM_CS_GPIO_Port,
    .cs_pin    = FRAM_CS_Pin,
    .wp_port   = FRAM_WP_GPIO_Port,
    .wp_pin    = FRAM_WP_Pin,
    .hold_port = FRAM_HOLD_GPIO_Port,
    .hold_pin  = FRAM_HOLD_Pin,
};

extern "C" void CY15B102Q_FaultCallback(CY15B102Q_FaultCode code) {
    Inverter::FaultReason reason = Inverter::FaultReason::FramCommandFailed;
    switch (code) {
        case CY15B102Q_FAULT_INIT_ID_MISMATCH:
            reason = Inverter::FaultReason::FramInitIdMismatch;
            break;
        case CY15B102Q_FAULT_READ_FAILED:
            reason = Inverter::FaultReason::FramReadFailed;
            break;
        case CY15B102Q_FAULT_WRITE_FAILED:
            reason = Inverter::FaultReason::FramWriteFailed;
            break;
        case CY15B102Q_FAULT_COMMAND_FAILED:
        default:
            reason = Inverter::FaultReason::FramCommandFailed;
            break;
    }
    Inverter::FaultManager::instance().raise(
        Inverter::FaultSource::FramComm, reason);
}

static void init()
{
    /* Initialize F-RAM for persistent on-time logging. */
    if (CY15B102Q_Init(&g_fram) == HAL_OK) {
        OnTime_Init(&g_fram);
    }

    /* Telemetry over the MCP2221A USB-UART bridge (USART3). */
    Telemetry::init();
    Telemetry::set_period_us(10000);  /* 100 Hz data frames */

    /* CAN error-status notifications (FDCAN2 is the active interface). */
    (void)Inverter::fdcanFaultInit();

    /* Supply rail monitoring (PVD/AVD/VOSRDY). */
    (void)Inverter::supplyMonitorInit();

    /* Enable the gate-driver power rail early so the isolated current sensors
     * and their references settle with the final supply configuration before
     * the zero-current offset is captured.  Keep the gate driver in reset so
     * the outputs stay disabled. */
    HAL_GPIO_WritePin(GATE_DRIVER_POWER_ENABLE_GPIO_Port,
                      GATE_DRIVER_POWER_ENABLE_Pin,
                      GPIO_PIN_SET);
    HAL_GPIO_WritePin(GATE_DRIVER_RESET_GPIO_Port,
                      GATE_DRIVER_RESET_Pin,
                      GPIO_PIN_RESET);
    HAL_Delay(500);

    /* Phase-current sensor test harness. */
    Inverter::CurrentSensorTest_Init();

    /* Open-loop motor control (PWM + gate driver). Default off.
     * This also performs the final current-sensor offset recalibration after
     * the gate-driver power rail is up and the PWM outputs are started, matching
     * the hardware state used by a manual `cal`. */
    Inverter::openLoopController().init();

    /* Closed-loop FOC manager.  Does not enable outputs until `foc` command. */
    Inverter::focControlManager().init();

    /* UART command shell for start/stop/freq/mod. */
    Inverter::commandShell().init();

    /* Enable the peripheral power rail that supplies the isolated ADC (VDDPL).
     * CurrentSensorTest_Init() also turns this on, but make sure it is high
     * before the isolated voltage sensor initializes. */
    HAL_GPIO_WritePin(PERIPHERAL_POWER_ENABLE_GPIO_Port,
                      PERIPHERAL_POWER_ENABLE_Pin,
                      GPIO_PIN_SET);
    HAL_Delay(200);

    /* Isolated high-voltage DC-link sensor on SPI2 (VSENSE_ISO_ADC_INTERRUPT = PD1). */
    Inverter::dcLinkVoltageSensor().init();
}

static void loop()
{
    static uint32_t s_last_ontime_ms = 0;

    const uint32_t now_ms = HAL_GetTick();

    /* On-time logger: update F-RAM only once per second. */
    if ((now_ms - s_last_ontime_ms) >= 1000U) {
        OnTime_Update();
        Telemetry::log("inv_ontime_ms",  static_cast<float>(OnTime_GetTotalMs()));
        Telemetry::log("inv_boot_count", static_cast<float>(OnTime_GetBootCount()));
        s_last_ontime_ms = now_ms;
    }

    /* Phase-current sensor test: read U, V, W at 100 Hz and publish over telemetry. */
    static uint32_t s_last_current_ms = 0;
    if ((now_ms - s_last_current_ms) >= 10U) {
        Inverter::CurrentSensorTest_RunOnce();
        s_last_current_ms = now_ms;
    }
    Inverter::encoderADC().diagnose();

    /* Isolated DC-link voltage sensor: sample on every loop so the logged
     * value is always the latest conversion from the EXTI ISR. */
    Inverter::dcLinkVoltageSensor().update();
    Inverter::supplyMonitorUpdate();

    /* Open-loop safety, calibration state machines, and command processing. */
    Inverter::FaultManager::instance().service();
    Inverter::commandShell().poll();
    Inverter::FaultManager::instance().executeSafetyActions();
    Inverter::openLoopController().update();
    Inverter::focControlManager().update();
    Inverter::poleCalibrator().update();
    Inverter::encoderOffsetCalibrator().update();
    Inverter::resistanceCalibrator().update();
    Inverter::autoCalibrationCoordinator().update();

    /* Telemetry for open-loop setpoints. */
    Telemetry::log("ol_freq_hz", Inverter::openLoopController().frequencyHz());
    Telemetry::log("ol_mod_idx", Inverter::openLoopController().modulationIndex());
    Telemetry::log("ol_running", Inverter::openLoopController().isRunning() ? 1.0f : 0.0f);

    Telemetry::updateSensors();
}

} // namespace InverterMain

extern "C" void InverterMain_Run(void)
{
    InverterMain::init();
    while (1) {
        InverterMain::loop();
    }
}
