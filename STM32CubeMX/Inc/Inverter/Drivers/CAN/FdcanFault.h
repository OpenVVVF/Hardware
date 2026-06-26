#pragma once

namespace Inverter {

/**
 * @brief Enable FDCAN error-status notifications and route them to FaultManager.
 *
 * Call after MX_FDCAN2_Init().
 */
bool fdcanFaultInit();

} // namespace Inverter
