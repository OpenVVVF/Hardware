#include "Inverter/InverterMain.h"
#include "Inverter/Telemetry.h"
#include "Inverter/Drivers/Sensors/CurrentSensorTest.h"
#include "Inverter/Drivers/Sensors/MAX22530.h"
#include "Inverter/Control/OpenLoopController.h"
#include "Inverter/Control/CommandShell.h"
#include "Inverter/Calibration/PolePairCalibrator.h"

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

static Inverter::MAX22530 s_vsense_iso(
    &hspi2,
    SPI2_CS_GPIO_Port, SPI2_CS_Pin,
    VSENSE_ISO_ADC_INTERRUPT_GPIO_Port, VSENSE_ISO_ADC_INTERRUPT_Pin,
    EXTI1_IRQn,
    "iso");

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

    /* Enable the peripheral power rail that supplies the isolated ADC (VDDPL).
     * The MAX22530 field-side DC-DC and ADC need this before conversions start. */
    HAL_GPIO_WritePin(PERIPHERAL_POWER_ENABLE_GPIO_Port,
                      PERIPHERAL_POWER_ENABLE_Pin,
                      GPIO_PIN_SET);
    HAL_Delay(200);

    /* Isolated high-voltage ADC on SPI2 (VSENSE_ISO_ADC_INTERRUPT = PD1). */
    s_vsense_iso.init();
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

    /* Isolated ADC on SPI2: read latest values at 100 Hz. */
    static uint32_t s_last_vsense_ms = 0;
    if ((now_ms - s_last_vsense_ms) >= 10U) {
        s_vsense_iso.update();
        s_last_vsense_ms = now_ms;
    }

    /* Open-loop safety, calibration state machines, and command processing. */
    Inverter::commandShell().poll();
    Inverter::openLoopController().update();
    Inverter::polePairCalibrator().update();

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
