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

/* During the first 500 ms after reset/flash the 3.3 V/VDDA rails can have
 * transients.  In that window a PVD/AVD event must still be present 50 ms
 * after the edge before it is raised.  After the startup window the fault is
 * raised immediately on the edge so operational supply ripples are caught
 * without delay. */
static constexpr uint32_t SUPPLY_STARTUP_WINDOW_MS = 500U;
static constexpr uint32_t SUPPLY_STARTUP_DEBOUNCE_MS = 50U;

static volatile uint32_t s_pvd_assert_ms = 0;
static volatile bool     s_pvd_pending   = false;
static volatile uint32_t s_avd_assert_ms = 0;
static volatile bool     s_avd_pending   = false;

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

    /* PVD and AVD share the PVD_AVD_IRQn vector.
     * Clear any stale EXTI/NVIC pending state left over from the debugger flash
     * or power-on transient before unmasking the interrupt, otherwise the old
     * event immediately raises a spurious SupplyPvd/SupplyAvd fault. */
    __HAL_PWR_PVD_EXTI_CLEAR_FLAG();
    __HAL_PWR_AVD_EXTI_CLEAR_FLAG();
    HAL_NVIC_ClearPendingIRQ(PVD_AVD_IRQn);

    HAL_NVIC_SetPriority(PVD_AVD_IRQn, 14, 0);
    HAL_NVIC_EnableIRQ(PVD_AVD_IRQn);

    return true;
}

void supplyMonitorUpdate() {
    if (__HAL_PWR_GET_FLAG(PWR_FLAG_VOSRDY) == 0U) {
        FaultManager::instance().raise(FaultSource::SupplyVosrdy,
                                       FaultReason::VosNotReady);
    }

    const uint32_t now = HAL_GetTick();

    if (s_pvd_pending &&
        ((now - s_pvd_assert_ms) >= SUPPLY_STARTUP_DEBOUNCE_MS)) {
        s_pvd_pending = false;
        if (__HAL_PWR_GET_FLAG(PWR_FLAG_PVDO) != 0U) {
            FaultManager::instance().raise(FaultSource::SupplyPvd,
                                           FaultReason::PvdTriggered);
        }
    }

    if (s_avd_pending &&
        ((now - s_avd_assert_ms) >= SUPPLY_STARTUP_DEBOUNCE_MS)) {
        s_avd_pending = false;
        if (__HAL_PWR_GET_FLAG(PWR_FLAG_AVDO) != 0U) {
            FaultManager::instance().raise(FaultSource::SupplyAvd,
                                           FaultReason::AvdTriggered);
        }
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
    const uint32_t now = HAL_GetTick();
    if (now >= Inverter::SUPPLY_STARTUP_WINDOW_MS) {
        Inverter::FaultManager::instance().raise(
            Inverter::FaultSource::SupplyPvd, Inverter::FaultReason::PvdTriggered);
        return;
    }
    Inverter::s_pvd_assert_ms = now;
    Inverter::s_pvd_pending = true;
}

extern "C" void HAL_PWREx_AVDCallback(void) {
    const uint32_t now = HAL_GetTick();
    if (now >= Inverter::SUPPLY_STARTUP_WINDOW_MS) {
        Inverter::FaultManager::instance().raise(
            Inverter::FaultSource::SupplyAvd, Inverter::FaultReason::AvdTriggered);
        return;
    }
    Inverter::s_avd_assert_ms = now;
    Inverter::s_avd_pending = true;
}
