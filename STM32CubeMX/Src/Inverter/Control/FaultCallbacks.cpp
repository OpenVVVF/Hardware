#include "Inverter/Control/FaultManager.h"
#include "Inverter/Drivers/GateDriver/gate_driver.h"

#include "main.h"
#include "tim.h"
#include "adc.h"
#include "usart.h"

namespace Inverter {

/* No C++ code needed in this file; the callbacks below are weak HAL symbols
 * that we override to latch hardware faults. */

} // namespace Inverter

extern "C" {

/* TIM1 break input (PE15).  The hardware break already disables TIM1 outputs;
 * this callback latches the event so the control loop can shut down cleanly. */
void TIM1_BRK_IRQHandler(void) {
    HAL_TIM_IRQHandler(&htim1);
}

void HAL_TIMEx_BreakCallback(TIM_HandleTypeDef* htim) {
    if (htim != nullptr && htim->Instance == TIM1) {
        /* The gate-driver /FLT pin is active-low and tied to TIM1_BKIN.
         * During boot the gate driver is held in reset, so /FLT may be low
         * and the break flag asserts even though there is no run-time fault.
         * Only latch a fault once the gate driver has left reset and is ready. */
        if (GateDriver_IsReady()) {
            Inverter::FaultManager::instance().raise(Inverter::FaultSource::PwmBreak);
        }
    }
}

/* ADC HAL errors on ADC1/ADC2 (phase-current sensing path). */
void HAL_ADC_ErrorCallback(ADC_HandleTypeDef* hadc) {
    if (hadc != nullptr && (hadc->Instance == ADC1 || hadc->Instance == ADC2)) {
        Inverter::FaultManager::instance().raise(Inverter::FaultSource::AdcError);
    }
}

/* USART3 shell/telemetry transport errors. */
void HAL_UART_ErrorCallback(UART_HandleTypeDef* huart) {
    if (huart != nullptr && huart->Instance == USART3) {
        Inverter::FaultManager::instance().raise(Inverter::FaultSource::UartError);
    }
}

} // extern "C"
