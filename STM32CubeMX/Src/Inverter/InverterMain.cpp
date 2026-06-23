#include "Inverter/InverterMain.h"
#include "Inverter/Telemetry.h"
#include "Inverter/Drivers/Sensors/CurrentSensorTest.h"
#include "Inverter/Control/OpenLoopController.h"
#include "Inverter/Control/CommandShell.h"

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

static void init()
{
    /* Initialize F-RAM for persistent on-time logging. */
    if (CY15B102Q_Init(&g_fram) == HAL_OK) {
        OnTime_Init(&g_fram);
    }

    /* Telemetry over the MCP2221A USB-UART bridge (USART3). */
    Telemetry::init();
    Telemetry::set_period_us(10000);  /* 100 Hz data frames */

    /* Phase-current sensor test harness. */
    Inverter::CurrentSensorTest_Init();

    /* Open-loop motor control (PWM + gate driver). Default off. */
    Inverter::openLoopController().init();

    /* UART command shell for start/stop/freq/mod. */
    Inverter::commandShell().init();
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

    /* Open-loop safety and command processing. */
    Inverter::commandShell().poll();
    Inverter::openLoopController().update();

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
