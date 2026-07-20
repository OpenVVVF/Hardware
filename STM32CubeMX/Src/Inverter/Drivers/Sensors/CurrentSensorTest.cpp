#include "Inverter/Drivers/Sensors/CurrentSensorTest.h"
#include "Inverter/Drivers/Sensors/PhaseCurrentADC.h"
#include "Inverter/Drivers/Sensors/EncoderADC.h"
#include "Inverter/Drivers/Sensors/PoleEstimator.h"
#include "Inverter/Telemetry.h"

#include "main.h"

namespace Inverter {

void CurrentSensorTest_Init() {
    /* Turn on the peripheral power rail that feeds the current sensors. */
    HAL_GPIO_WritePin(PERIPHERAL_POWER_ENABLE_GPIO_Port,
                      PERIPHERAL_POWER_ENABLE_Pin,
                      GPIO_PIN_SET);

    /* Allow the LA37S600 sensors and their 5 V rail to settle. */
    HAL_Delay(500);

    /* Set up PWM-synchronized U/V current ADC and start conversions.
     * PhaseCurrentADC::start() now waits 2 s for the sensor to warm up
     * before running the zero-current calibration. */
    PhaseCurrentADC& adc = phaseCurrentADC();
    adc.init();
    adc.start();

    /* Set up motor encoder (ADC2 regular + DMA2_Stream0 + TIM2 trigger). */
    EncoderADC& enc = encoderADC();
    enc.init();
    enc.start();
}

void CurrentSensorTest_RunOnce() {
    static uint32_t s_last_offset_ms = 0;
    float iu = 0.0f, iv = 0.0f, iw = 0.0f;
    /* Use the non-destructive accessor: FOC consumes sample() from the PWM
     * ISR at the control rate, and racing it for the freshness flag would
     * leave the telemetry path starved of new values. */
    if (phaseCurrentADC().latest(iu, iv, iw)) {
        /* Do not publish current values until the zero-current offset has
         * been validated.  This prevents huge phantom startup spikes from
         * appearing in the telemetry log when the sensor is still settling. */
        if (phaseCurrentADC().offsetValid()) {
            Telemetry::log("ph_u_a", iu);
            Telemetry::log("ph_v_a", iv);
            Telemetry::log("ph_w_a", iw);
        }
        Telemetry::log("ph_cal_valid", phaseCurrentADC().offsetValid() ? 1.0f : 0.0f);

        /* Diagnostics for tuning offset/noise. */
        PhaseCurrentADC& adc = phaseCurrentADC();
        Telemetry::log("ph_u_sig", static_cast<float>(adc.lastRawUSig()));
        Telemetry::log("ph_v_sig", static_cast<float>(adc.lastRawVSig()));
        Telemetry::log("ph_u_ref", static_cast<float>(adc.lastRawURef()));
        Telemetry::log("ph_v_ref", static_cast<float>(adc.lastRawVRef()));

        /* Log offsets periodically so drift/recalibration is visible. */
        const uint32_t now_ms = HAL_GetTick();
        if ((now_ms - s_last_offset_ms) >= 1000U) {
            s_last_offset_ms = now_ms;
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

    /* Pole estimate: refines continuously while the motor is running. */
    PoleEstimator& poles = PoleEstimator::instance();
    Telemetry::log("pole_estimate", poles.estimate());
    Telemetry::log("pole_window", poles.windowEstimate());
    Telemetry::log("pole_mech_cycles", poles.mechanicalCycles());
    Telemetry::log("pole_elec_cycles", poles.electricalCycles());
}

} // namespace Inverter
