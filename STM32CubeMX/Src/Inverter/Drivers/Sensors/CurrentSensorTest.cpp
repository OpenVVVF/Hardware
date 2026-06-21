#include "Inverter/Drivers/Sensors/CurrentSensorTest.h"
#include "Inverter/Drivers/Sensors/PhaseCurrentADC.h"
#include "Inverter/Drivers/Sensors/EncoderADC.h"
#include "Inverter/Telemetry.h"

#include "main.h"

namespace Inverter {

void CurrentSensorTest_Init() {
    /* Turn on the peripheral power rail that feeds the current sensors. */
    HAL_GPIO_WritePin(PERIPHERAL_POWER_ENABLE_GPIO_Port,
                      PERIPHERAL_POWER_ENABLE_Pin,
                      GPIO_PIN_SET);

    /* Allow the LA37S600 sensors and their 5 V rail to settle.
     * Hall sensors can drift for several hundred ms after power-on, so give
     * them a full second before the zero-current calibration runs. */
    HAL_Delay(100);

    /* Set up PWM-synchronized U/V current ADC and start conversions. */
    PhaseCurrentADC& adc = phaseCurrentADC();
    adc.init();
    adc.start();

    /* Set up motor encoder (ADC2 regular + DMA2_Stream0 + TIM2 trigger). */
    EncoderADC& enc = encoderADC();
    enc.init();
    enc.start();
}

void CurrentSensorTest_RunOnce() {
    static bool s_logged_offsets = false;
    float iu = 0.0f, iv = 0.0f, iw = 0.0f;
    if (phaseCurrentADC().sample(iu, iv, iw)) {
        Telemetry::log("ph_u_a", iu);
        Telemetry::log("ph_v_a", iv);
        Telemetry::log("ph_w_a", iw);

        /* Diagnostics for tuning offset/noise. */
        PhaseCurrentADC& adc = phaseCurrentADC();
        Telemetry::log("ph_u_sig", static_cast<float>(adc.lastRawUSig()));
        Telemetry::log("ph_v_sig", static_cast<float>(adc.lastRawVSig()));
        Telemetry::log("ph_u_ref", static_cast<float>(adc.lastRawURef()));
        Telemetry::log("ph_v_ref", static_cast<float>(adc.lastRawVRef()));

        if (!s_logged_offsets) {
            s_logged_offsets = true;
            Telemetry::log("ph_u_offset", adc.lastOffsetU());
            Telemetry::log("ph_v_offset", adc.lastOffsetV());
        }
    }

    float enc_angle = 0.0f;
    if (encoderADC().sample(enc_angle)) {
        Telemetry::log("enc_angle_deg", enc_angle);
        Telemetry::log("enc_raw_sin", static_cast<float>(encoderADC().lastRawSin()));
        Telemetry::log("enc_raw_cos", static_cast<float>(encoderADC().lastRawCos()));
    }
}

} // namespace Inverter
