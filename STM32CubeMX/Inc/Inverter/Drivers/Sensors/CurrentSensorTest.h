#pragma once

namespace Inverter {

/**
 * @brief Initialises the three phase-current sensors and the telemetry keys.
 */
void CurrentSensorTest_Init();

/**
 * @brief Samples U, V and W currents and publishes them over telemetry.
 *
 * Call at the desired telemetry rate.  Each call performs one round of
 * measurements and queues the values; the telemetry layer sends them at
 * its configured frame rate.
 */
void CurrentSensorTest_RunOnce();

} // namespace Inverter
