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
 */
struct MotorCalibration {
    float pole_count = 0.0f;              /**< Total rotor pole count. */
    float encoder_cycles_per_rev = 0.0f;  /**< Encoder electrical cycles per mech rev. */
    float encoder_offset_deg = 0.0f;      /**< Encoder offset, mechanical degrees. */
    float r_phase_uv = 0.0f;              /**< Per-phase resistance from UV pair [ohm]. */
    float r_phase_uw = 0.0f;              /**< Per-phase resistance from UW pair [ohm]. */
    float r_phase_vw = 0.0f;              /**< Per-phase resistance from VW pair [ohm]. */
    float r_phase_avg = 0.0f;             /**< Average per-phase resistance [ohm]. */
    uint32_t timestamp_ms = 0;            /**< HAL tick when the calibration finished. */
    bool valid = false;                   /**< True after a successful calibration. */

    static MotorCalibration& instance();
};

/**
 * @brief Global accessor for the latest motor calibration.
 */
MotorCalibration& motorCalibration();

} // namespace Inverter
