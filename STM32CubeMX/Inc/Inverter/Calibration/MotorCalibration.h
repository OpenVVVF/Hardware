#pragma once

#include <cstdint>

namespace Inverter {

/**
 * @brief Latest successfully calibrated motor parameters.
 *
 * Updated automatically when the automatic motor profiling routine finishes.
 * FOC and other control code can read these values directly; they are also
 * emitted as telemetry keys so a host can record them.
 *
 * Values are held in RAM only and reset to invalid on boot.  To make them
 * persistent across power cycles, the host must capture them from telemetry
 * (or this struct) and write them to flash/EEPROM.
 *
 * NOTE: The defaults below are the values from a successful motor profile run.
 * They are hardcoded here for debugging so FOC can start without rerunning the
 * full calibration routine.  Replace them with your own captured values if
 * the motor or encoder hardware changes.
 */
struct MotorCalibration {
    float pole_count = 10.0f;             /**< Total rotor pole count. */
    float encoder_cycles_per_rev = 1.0f;  /**< Encoder electrical cycles per mech rev. */
    float encoder_offset_deg = 13.106f;   /**< Encoder offset, mechanical degrees. */
    float encoder_sign = -1.0f;           /**< +1 or -1: encoder direction vs rotor field. */
    float r_phase_uv = 0.0144f;           /**< Per-phase resistance from UV pair [ohm]. */
    float r_phase_uw = 0.0150f;           /**< Per-phase resistance from UW pair [ohm]. */
    float r_phase_vw = 0.0141f;           /**< Per-phase resistance from VW pair [ohm]. */
    float r_phase_avg = 0.0145f;          /**< Average per-phase resistance [ohm]. */
    float ld_henry = 0.0f;                /**< d-axis inductance (0 = not calibrated). */
    float lq_henry = 0.0f;                /**< q-axis inductance (0 = not calibrated). */
    uint32_t timestamp_ms = 0;            /**< HAL tick when the calibration finished. */
    bool valid = true;                    /**< True after a successful calibration. */

    static MotorCalibration& instance();
};

/**
 * @brief Global accessor for the latest motor calibration.
 */
MotorCalibration& motorCalibration();

} // namespace Inverter
