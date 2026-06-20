#include "Inverter/InverterMain.h"
#include "Inverter/Telemetry.h"

#include "main.h"
#include "spi.h"
#include "cy15b102q_driver.h"
#include "ontime_logger.h"

#include <cmath>

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
}

static void loop()
{
    static uint32_t s_last_ontime_ms = 0;
    static float    s_sine_angle = 0.0f;

    const uint32_t now_ms = HAL_GetTick();

    /* On-time logger: update F-RAM only once per second. */
    if ((now_ms - s_last_ontime_ms) >= 1000U) {
        OnTime_Update();
        Telemetry::log("inv_ontime_ms",  static_cast<float>(OnTime_GetTotalMs()));
        Telemetry::log("inv_boot_count", static_cast<float>(OnTime_GetBootCount()));
        s_last_ontime_ms = now_ms;
    }

    /* 100 Hz test sine wave (1 Hz tone). */
    Telemetry::log("test_sine", std::sin(s_sine_angle));
    if (Telemetry::updateSensors()) {
        s_sine_angle += 2.0f * 3.14159265f / 100.0f;
        if (s_sine_angle >= 2.0f * 3.14159265f) {
            s_sine_angle -= 2.0f * 3.14159265f;
        }
    }
}

} // namespace InverterMain

extern "C" void InverterMain_Run(void)
{
    InverterMain::init();
    while (1) {
        InverterMain::loop();
    }
}
