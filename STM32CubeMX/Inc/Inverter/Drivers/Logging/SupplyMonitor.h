#pragma once

namespace Inverter {

/**
 * @brief Configure and monitor STM32 supply rails.
 *
 * Enables PVD (VDD), AVD (VDDA) and VOSRDY monitoring.  Critical faults are
 * raised on any supply anomaly.
 */
bool supplyMonitorInit();
void supplyMonitorUpdate();
void supplyMonitorPrintStatus();

} // namespace Inverter
