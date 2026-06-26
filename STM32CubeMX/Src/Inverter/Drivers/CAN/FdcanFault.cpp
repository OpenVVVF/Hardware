#include "Inverter/Drivers/CAN/FdcanFault.h"
#include "Inverter/Control/FaultManager.h"

#include "fdcan.h"

namespace Inverter {

bool fdcanFaultInit() {
    constexpr uint32_t kNotify = FDCAN_IT_BUS_OFF |
                                 FDCAN_IT_ERROR_PASSIVE |
                                 FDCAN_IT_ERROR_LOGGING_OVERFLOW;
    if (HAL_FDCAN_ActivateNotification(&hfdcan2, kNotify, 0) != HAL_OK) {
        return false;
    }
    return true;
}

} // namespace Inverter

extern "C" void HAL_FDCAN_ErrorStatusCallback(FDCAN_HandleTypeDef* hfdcan,
                                              uint32_t ErrorStatusITs) {
    if (hfdcan == nullptr) {
        return;
    }
    if (hfdcan->Instance == FDCAN2) {
        if ((ErrorStatusITs & FDCAN_FLAG_BUS_OFF) != 0) {
            Inverter::FaultManager::instance().raise(
                Inverter::FaultSource::CanBusOff, Inverter::FaultReason::CanBusOff);
        }
        if ((ErrorStatusITs & FDCAN_FLAG_ERROR_PASSIVE) != 0) {
            Inverter::FaultManager::instance().raise(
                Inverter::FaultSource::CanErrorPassive, Inverter::FaultReason::CanErrorPassive);
        }
        if ((ErrorStatusITs & FDCAN_FLAG_ERROR_LOGGING_OVERFLOW) != 0) {
            Inverter::FaultManager::instance().raise(
                Inverter::FaultSource::CanProtocolError,
                Inverter::FaultReason::CanErrorLogOverflow);
        }
    }
}

extern "C" void HAL_FDCAN_ErrorCallback(FDCAN_HandleTypeDef* hfdcan) {
    if (hfdcan != nullptr && hfdcan->Instance == FDCAN2) {
        Inverter::FaultManager::instance().raise(
            Inverter::FaultSource::CanProtocolError, Inverter::FaultReason::CanHalError);
    }
}
