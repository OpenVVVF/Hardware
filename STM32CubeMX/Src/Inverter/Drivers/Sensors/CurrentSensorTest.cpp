#include "Inverter/Drivers/Sensors/CurrentSensorTest.h"
#include "Inverter/Drivers/Sensors/CurrentSensor.h"
#include "Inverter/Telemetry.h"

#include "main.h"

#include <cstring>
#include <cstdio>

namespace Inverter {

/* LA37S600S05K(M) scaling
 *
 * Datasheet: Vcc = 5 V, internal Vref = 2.5 V, IpN = 600 A.
 * Sensitivity = 1.042 mV/A (the output changes 1.042 mV per amp from Vref).
 * Equivalently the datasheet states ΔVout = 625 mV @ IpN:
 *   625 mV / 600 A = 1.042 mV/A.
 *
 * Board divider: 10 k series + 20 k shunt (+ 10 nF) -> 20k/(10k+20k) = 2/3.
 * This scales the sensor's ~5 V swing down to the 3.3 V ADC range.
 *
 * CurrentSensor::read() converts raw ADC codes to MCU volts, divides by the
 * divider ratio to recover sensor-side voltages, then computes:
 *   I = (Vout - Vref) / sensitivity.
 * Because the divider is removed before the subtraction, we pass the
 * sensor-side sensitivity here.
 */
static constexpr float SENSITIVITY_VA = 1.042e-3f; /**< V/A at sensor pin. */
static constexpr float DIVIDER        = 2.0f / 3.0f;
static constexpr float ADC_VREF       = 3.3f;

static CurrentSensor s_sensor_u;
static CurrentSensor s_sensor_v;
static CurrentSensor s_sensor_w;

void CurrentSensorTest_Init() {
    /* Turn on the peripheral power rail that feeds the current sensors. */
    HAL_GPIO_WritePin(PERIPHERAL_POWER_ENABLE_GPIO_Port,
                      PERIPHERAL_POWER_ENABLE_Pin,
                      GPIO_PIN_SET);

    /* CubeMX configured ADC1/ADC2 for 16x oversampling with no right shift,
     * which makes HAL_ADC_GetValue return the 16-sample *sum* instead of the
     * average.  Fix the shift so the hardware returns the averaged value. */
    MODIFY_REG(hadc1.Instance->CFGR2, ADC_CFGR2_OVSS, ADC_RIGHTBITSHIFT_4);
    MODIFY_REG(hadc2.Instance->CFGR2, ADC_CFGR2_OVSS, ADC_RIGHTBITSHIFT_4);

    /* Phase U: ADC1 CH4 (PC4) = Vout, ADC2 CH8 (PC5) = Vref.
     * Dual-ADC regular-simultaneous sampling gives exact same-instant Vout/Vref. */
    s_sensor_u.init({
        .name = "ph_u",
        .output   = { &hadc1, ADC_CHANNEL_4 },
        .reference= { &hadc2, ADC_CHANNEL_8 },
        .vref_mcu = ADC_VREF,
        .adc_bits = 16,
        .voltage_divider = DIVIDER,
        .sensitivity_va  = SENSITIVITY_VA,
    });

    /* Phase V: ADC1 CH3 (PA6) = Vout, ADC2 CH7 (PA7) = Vref. */
    s_sensor_v.init({
        .name = "ph_v",
        .output   = { &hadc1, ADC_CHANNEL_3 },
        .reference= { &hadc2, ADC_CHANNEL_7 },
        .vref_mcu = ADC_VREF,
        .adc_bits = 16,
        .voltage_divider = DIVIDER,
        .sensitivity_va  = SENSITIVITY_VA,
    });

    /* Phase W: ADC3 CH1 (PC3_C) = Vout, ADC3 CH0 (PC2_C) = Vref.
     * ADC3 has no dual partner in this pinout, so it samples sequentially. */
    s_sensor_w.init({
        .name = "ph_w",
        .output   = { &hadc3, ADC_CHANNEL_1 },
        .reference= { &hadc3, ADC_CHANNEL_0 },
        .vref_mcu = ADC_VREF,
        .adc_bits = 12,
        .voltage_divider = DIVIDER,
        .sensitivity_va  = SENSITIVITY_VA,
    });
}

static void publishSensor(CurrentSensor& sensor) {
    float current_a, vout_v, vref_v;
    if (!sensor.read(current_a, vout_v, vref_v)) {
        return;
    }

    char key[32];
    const char* name = sensor.name();

    std::snprintf(key, sizeof(key), "%s_a", name);
    Telemetry::log(key, current_a);

    std::snprintf(key, sizeof(key), "%s_vout", name);
    Telemetry::log(key, vout_v);

    std::snprintf(key, sizeof(key), "%s_vref", name);
    Telemetry::log(key, vref_v);
}

void CurrentSensorTest_RunOnce() {
    publishSensor(s_sensor_u);
    publishSensor(s_sensor_v);
    publishSensor(s_sensor_w);
}

} // namespace Inverter
