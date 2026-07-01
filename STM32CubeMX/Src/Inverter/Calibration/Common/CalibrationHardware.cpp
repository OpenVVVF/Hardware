#include "Inverter/Calibration/Common/CalibrationHardware.h"

#include "Inverter/Drivers/GateDriver/gate_driver.h"
#include "Inverter/Drivers/PWM/pwm.h"
#include "Inverter/Telemetry.h"

#include "main.h"

namespace Inverter {

void CalibrationHardware::begin() {
    GateDriver_EnablePower(true);
    assertOutputs();
    m_state_start_ms = HAL_GetTick();
    m_state = State::POWER_STABILIZE;
}

void CalibrationHardware::update() {
    const uint32_t now_ms = HAL_GetTick();

    switch (m_state) {
        case State::POWER_STABILIZE:
            if ((now_ms - m_state_start_ms) >= 50U) {
                releaseOutputs();
                m_state_start_ms = now_ms;
                m_state = State::READY_WAIT;
            }
            break;

        case State::READY_WAIT:
            if (GateDriver_IsReady() && !GateDriver_IsFault()) {
                m_state = State::READY;
            } else if ((now_ms - m_state_start_ms) > 500U) {
                Telemetry::printf("[CAL] HW: gate driver not ready | ready=%s fault=%s",
                                  GateDriver_IsReady() ? "Y" : "N",
                                  GateDriver_IsFault() ? "Y" : "N");
                shutdown();
                m_state = State::FAILED;
            }
            break;

        case State::READY:
        case State::FAILED:
        case State::IDLE:
        default:
            break;
    }
}

void CalibrationHardware::releaseOutputs() {
    HAL_GPIO_WritePin(GATE_DRIVER_RESET_GPIO_Port, GATE_DRIVER_RESET_Pin, GPIO_PIN_SET);
}

void CalibrationHardware::assertOutputs() {
    HAL_GPIO_WritePin(GATE_DRIVER_RESET_GPIO_Port, GATE_DRIVER_RESET_Pin, GPIO_PIN_RESET);
}

void CalibrationHardware::parkOutputs() {
    PWM_SetThreePhaseDuty(50.0f, 50.0f, 50.0f);
}

void CalibrationHardware::shutdown() {
    PWM_StopSPWM();
    parkOutputs();
    assertOutputs();
}

} // namespace Inverter
