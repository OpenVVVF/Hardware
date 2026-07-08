#pragma once

#include "Inverter/Calibration/MotorCalibration.h"

namespace Inverter {

/**
 * @brief All motor/inverter parameters needed by the FOC controller.
 *
 * Ld, Lq, and flux linkage are hardcoded to the PicoFirmware defaults for now.
 * Inductance/flux calibration will be added later.
 */
struct MotorParameters {
    float pole_pairs = 5.0f;
    float ld_henry = 40.0e-6f;
    float lq_henry = 40.0e-6f;
    float flux_linkage_wb = 0.0f;

    float encoder_offset_rad = 0.0f;      /**< Electrical-angle offset [rad]. */
    float encoder_cycles_per_rev = 1.0f;
    float encoder_sign = -1.0f;           /**< +1 or -1: encoder direction vs rotor field. */

    float max_phase_current_a = 40.0f;
    float max_modulation = 0.9f;

    float vdc_v = 0.0f;
};

/**
 * @brief Build FOC motor parameters from the latest calibration + hardcoded values.
 */
inline MotorParameters buildMotorParametersFromCalibration(const MotorCalibration& cal,
                                                            float vdc_v) {
    MotorParameters p;
    p.vdc_v = vdc_v;

    if (cal.valid && cal.pole_count > 0.0f) {
        p.pole_pairs = cal.pole_count * 0.5f;
        if (cal.encoder_cycles_per_rev > 0.0f) {
            p.encoder_cycles_per_rev = cal.encoder_cycles_per_rev;
        }
        /* The stored calibration offset is mechanical degrees.  Convert to
         * electrical radians so it can be added directly to
         * encoder_angle * pole_pairs / encoder_cycles_per_rev. */
        const float mech_offset_rad = cal.encoder_offset_deg *
                                      (3.14159265358979323846f / 180.0f);
        p.encoder_offset_rad = mech_offset_rad * p.pole_pairs / p.encoder_cycles_per_rev;
        p.encoder_sign = (cal.encoder_sign >= 0.0f) ? 1.0f : -1.0f;
    }

    // Hardcoded values carried over from PicoFirmware until calibration exists.
    p.ld_henry = 40.0e-6f;
    p.lq_henry = 40.0e-6f;
    p.flux_linkage_wb = 0.0f;
    p.max_phase_current_a = 40.0f;
    p.max_modulation = 0.9f;

    return p;
}

} // namespace Inverter
