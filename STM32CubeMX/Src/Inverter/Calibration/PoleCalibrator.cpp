#include "Inverter/Calibration/PoleCalibrator.h"

#include "Inverter/Control/OpenLoopController.h"
#include "Inverter/Drivers/Sensors/EncoderADC.h"
#include "Inverter/Drivers/Sensors/PoleEstimator.h"
#include "Inverter/Drivers/PWM/pwm.h"
#include "Inverter/Telemetry.h"

#include "main.h"
#include <cmath>

namespace Inverter {

static PoleCalibrator s_instance;

PoleCalibrator& PoleCalibrator::instance() {
    return s_instance;
}

PoleCalibrator& poleCalibrator() {
    return s_instance;
}

bool PoleCalibrator::start(float max_mod, float torque_margin) {
    if (openLoopController().isRunning()) {
        Telemetry::printf("[CAL] POLES: stop the motor before starting calibration");
        return false;
    }

    if (max_mod <= 0.0f) max_mod = 0.50f;
    if (torque_margin <= 0.0f) torque_margin = 0.40f;
    m_max_mod = max_mod;

    /* Start open-loop at 2 Hz, zero modulation. */
    if (!openLoopController().start(2.0f, 0.0f)) {
        Telemetry::printf("[CAL] POLES: ERROR: failed to start open-loop controller");
        return false;
    }

    PWM_ResetSPWMElectricalCycles();

    m_mod = 0.0f;
    m_breakaway_found = false;
    m_breakaway_mod = 0.0f;
    m_breakaway_mech_cycles = 0.0f;
    m_last_poles = 0.0f;

    m_tracker.reset();
    m_breakaway.start(0.01f, 50U, max_mod, 0.15f, torque_margin, 3000U);

    m_state = State::RAMP;

    Telemetry::printf("[CAL] POLES: started at 2 Hz, max_mod=%.3f margin=%.3f",
                      static_cast<double>(max_mod),
                      static_cast<double>(torque_margin));
    return true;
}

void PoleCalibrator::stop() {
    if (m_state != State::IDLE && m_state != State::DONE && m_state != State::FAIL) {
        openLoopController().stop();
        m_state = State::IDLE;
        Telemetry::printf("[CAL] POLES: stopped by user");
    }
}

void PoleCalibrator::reportPoles(const char* label) {
    const float mech_cycles = PoleEstimator::instance().mechanicalCycles();
    const float cycles_counted = mech_cycles - m_mech_count_start;
    const uint32_t elec_counted = PWM_GetSPWMElectricalCycles() - m_elec_count_start;
    const float poles = (cycles_counted > 0.0f)
                            ? 2.0f * static_cast<float>(elec_counted) / cycles_counted
                            : 0.0f;
    m_last_poles = poles;

    Telemetry::printf("[CAL] POLES: %s: poles=%.3f at mod=%.3f (elec=%lu mech=%.2f)",
                      label, poles, m_mod,
                      static_cast<unsigned long>(elec_counted),
                      cycles_counted);
}

void PoleCalibrator::update() {
    if (m_state == State::IDLE || m_state == State::DONE || m_state == State::FAIL) {
        return;
    }

    if (!openLoopController().isRunning()) {
        m_state = State::IDLE;
        return;
    }

    m_tracker.update();

    constexpr float TARGET_MECH_CYCLES = 1.0f;
    constexpr float MIN_PARTIAL_CYCLES = 0.5f;
    constexpr uint32_t STALL_TIMEOUT_MS = 3000U;
    constexpr uint32_t MAX_COUNT_MS = 120000U;

    const uint32_t now_ms = HAL_GetTick();
    const float mech_cycles = PoleEstimator::instance().mechanicalCycles();
    const float angle_cycles = std::fabs(m_tracker.mechanicalCycles());

    if (m_state == State::RAMP) {
        if (!m_breakaway_found) {
            float mod = 0.0f;
            const auto status = m_breakaway.update(now_ms, angle_cycles, mod);

            if (status == BreakawayFinder::Status::RUNNING) {
                m_mod = mod;
                openLoopController().setModulationIndexDirect(m_mod);

                if (m_tracker.stalled(now_ms, STALL_TIMEOUT_MS) && m_mod >= m_max_mod) {
                    m_state = State::FAIL;
                    Telemetry::printf("[CAL] POLES: FAIL: encoder did not move");
                    openLoopController().stop();
                }
                return;
            }

            if (status == BreakawayFinder::Status::TIMEOUT) {
                m_state = State::FAIL;
                Telemetry::printf("[CAL] POLES: FAIL: encoder did not move");
                openLoopController().stop();
                return;
            }

            /* Breakaway found. */
            m_breakaway_found = true;
            m_breakaway_mod = m_breakaway.breakawayMod();
            m_mod = mod;  /* already boosted by BreakawayFinder */
            m_breakaway_mech_cycles = mech_cycles;
            openLoopController().setModulationIndexDirect(m_mod);
            Telemetry::printf("[CAL] POLES: breakaway at mod=%.3f, holding at %.3f",
                              static_cast<double>(m_breakaway_mod),
                              static_cast<double>(m_mod));
            return;
        }

        /* Wait for the next zero-crossing so the measurement window starts at a
         * robust cycle boundary. */
        if (mech_cycles > m_breakaway_mech_cycles) {
            m_mech_count_start = mech_cycles;
            m_elec_count_start = PWM_GetSPWMElectricalCycles();
            m_count_start_ms = now_ms;
            m_state = State::COUNT;
            Telemetry::printf("[CAL] POLES: zero crossing, counting one cycle");
        }
        return;
    }
    else if (m_state == State::COUNT) {
        const float cycles_counted = mech_cycles - m_mech_count_start;

        if (m_tracker.stalled(now_ms, STALL_TIMEOUT_MS)) {
            if (cycles_counted >= MIN_PARTIAL_CYCLES) {
                m_state = State::DONE;
                reportPoles("partial");
            } else {
                m_state = State::FAIL;
                Telemetry::printf("[CAL] POLES: FAIL: encoder stalled during count");
            }
            openLoopController().stop();
            return;
        }

        if ((now_ms - m_count_start_ms) > MAX_COUNT_MS) {
            m_state = State::FAIL;
            Telemetry::printf("[CAL] POLES: FAIL: count took too long");
            openLoopController().stop();
            return;
        }

        if (cycles_counted >= TARGET_MECH_CYCLES) {
            reportPoles("done");
            m_state = State::DONE;
            openLoopController().stop();
        }
    }
}

} // namespace Inverter
