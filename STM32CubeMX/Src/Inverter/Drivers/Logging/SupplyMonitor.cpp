#include "Inverter/Drivers/Logging/SupplyMonitor.h"
#include "Inverter/Control/FaultManager.h"
#include "Inverter/Telemetry.h"

#include "main.h"
#include "stm32h7xx_hal.h"
#include "stm32h7xx_hal_pwr_ex.h"

#include <cstdio>

namespace Inverter {

/* PVD level: ~2.7 V falling-edge threshold for a 3.3 V VDD rail.
 * Adjust if the board uses a different nominal VDD. */
static constexpr uint32_t PVD_LEVEL = PWR_PVDLEVEL_5;

/* AVD level: level 3 corresponds to ~2.7 V on VDDA (see datasheet).
 * Adjust if the board uses a different nominal VDDA. */
static constexpr uint32_t AVD_LEVEL = PWR_AVDLEVEL_3;

bool supplyMonitorInit() {
    /* PVD monitors VDD. */
    PWR_PVDTypeDef pvd = {};
    pvd.PVDLevel = PVD_LEVEL;
    pvd.Mode = PWR_PVD_MODE_IT_FALLING;
    HAL_PWR_ConfigPVD(&pvd);
    HAL_PWR_EnablePVD();

    /* AVD monitors VDDA. */
    PWREx_AVDTypeDef avd = {};
    avd.AVDLevel = AVD_LEVEL;
    avd.Mode = PWR_AVD_MODE_IT_FALLING;
    HAL_PWREx_ConfigAVD(&avd);
    HAL_PWREx_EnableAVD();

    /* PVD and AVD share the PVD_AVD_IRQn vector. */
    HAL_NVIC_SetPriority(PVD_AVD_IRQn, 14, 0);
    HAL_NVIC_EnableIRQ(PVD_AVD_IRQn);

    return true;
}

void supplyMonitorUpdate() {
    if (__HAL_PWR_GET_FLAG(PWR_FLAG_VOSRDY) == 0U) {
        FaultManager::instance().raise(FaultSource::SupplyVosrdy,
                                       FaultReason::VosNotReady);
    }
}

void supplyMonitorPrintStatus() {
    const bool vos  = __HAL_PWR_GET_FLAG(PWR_FLAG_VOSRDY) != 0U;
    const bool pvdo = __HAL_PWR_GET_FLAG(PWR_FLAG_PVDO) != 0U;
    const bool avdo = __HAL_PWR_GET_FLAG(PWR_FLAG_AVDO) != 0U;
    char msg[80];
    std::snprintf(msg, sizeof(msg),
                  "[SHELL] supply: VOSRDY=%s PVD=%s AVD=%s",
                  vos ? "Y" : "N", pvdo ? "Y" : "N", avdo ? "Y" : "N");
    Telemetry::log("print", msg);
}

} // namespace Inverter

extern "C" void PVD_AVD_IRQHandler(void) {
    HAL_PWREx_PVD_AVD_IRQHandler();
}

extern "C" void HAL_PWR_PVDCallback(void) {
    Inverter::FaultManager::instance().raise(
        Inverter::FaultSource::SupplyPvd, Inverter::FaultReason::PvdTriggered);
}

extern "C" void HAL_PWREx_AVDCallback(void) {
    Inverter::FaultManager::instance().raise(
        Inverter::FaultSource::SupplyAvd, Inverter::FaultReason::AvdTriggered);
}
