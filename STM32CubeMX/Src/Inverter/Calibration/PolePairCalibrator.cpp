#include "Inverter/Calibration/PolePairCalibrator.h"

#include "Inverter/Control/OpenLoopController.h"
#include "Inverter/Drivers/Sensors/EncoderADC.h"
#include "Inverter/Drivers/Sensors/PolePairEstimator.h"
#include "Inverter/Drivers/PWM/pwm.h"
#include "Inverter/Telemetry.h"

#include "main.h"
#include <cstdio>
#include <cmath>

namespace Inverter {

static PolePairCalibrator s_instance;

PolePairCalibrator& PolePairCalibrator::instance() {
    return s_instance;
}

PolePairCalibrator& polePairCalibrator() {
    return s_instance;
}

namespace {

void fmtFloat2(char* buf, size_t cap, float v) {
    int whole = static_cast<int>(v);
    int frac = static_cast<int>((v - whole) * 100.0f + 0.5f);
    if (frac < 0) frac = -frac;
    std::snprintf(buf, cap, "%d.%02d", whole, frac);
}

void fmtFloat3(char* buf, size_t cap, float v) {
    int whole = static_cast<int>(v);
    int frac = static_cast<int>((v - whole) * 1000.0f + 0.5f);
    if (frac < 0) frac = -frac;
    std::snprintf(buf, cap, "%d.%03d", whole, frac);
}

} // namespace

bool PolePairCalibrator::start() {
    if (openLoopController().isRunning()) {
        Telemetry::log("print", "[CAL PP] stop the motor before starting calibration");
        return false;
    }

    /* Start open-loop at 2 Hz, zero modulation. */
    if (!openLoopController().start(2.0f, 0.0f)) {
        Telemetry::log("print", "[CAL PP] ERROR: failed to start open-loop controller");
        return false;
    }

    PWM_ResetSPWMElectricalCycles();

    m_mod = 0.0f;
    m_breakaway_detected = false;
    m_breakaway_mod = 0.0f;
    m_breakaway_mech_cycles = 0.0f;
    m_last_ratio = 0.0f;
    m_unwrapped_angle = 0.0f;
    m_last_angle = encoderADC().lastAngle();
    m_cycles_at_last_move = 0.0f;
    m_last_ramp_ms = HAL_GetTick();
    m_last_move_ms = HAL_GetTick();
    m_state = State::RAMP;

    Telemetry::log("print", "[CAL PP] started at 2 Hz, fast ramp to breakaway");
    return true;
}

void PolePairCalibrator::sampleEncoderAngle() {
    const float angle = encoderADC().lastAngle();
    float delta = angle - m_last_angle;
    if (delta > 180.0f) {
        delta -= 360.0f;
    } else if (delta < -180.0f) {
        delta += 360.0f;
    }
    m_unwrapped_angle += delta;
    m_last_angle = angle;
}

void PolePairCalibrator::reportRatio(const char* label) {
    const float mech_cycles = PolePairEstimator::instance().mechanicalCycles();
    const float cycles_counted = mech_cycles - m_mech_count_start;
    const uint32_t elec_counted = PWM_GetSPWMElectricalCycles() - m_elec_count_start;
    const float ratio = (cycles_counted > 0.0f)
                            ? static_cast<float>(elec_counted) / cycles_counted
                            : 0.0f;
    m_last_ratio = ratio;

    char ratio_buf[16];
    fmtFloat3(ratio_buf, sizeof(ratio_buf), ratio);
    char mod_buf[16];
    fmtFloat3(mod_buf, sizeof(mod_buf), m_mod);
    char mech_buf[16];
    fmtFloat2(mech_buf, sizeof(mech_buf), cycles_counted);
    char msg[96];
    std::snprintf(msg, sizeof(msg),
                  "[CAL PP] %s: ratio=%s at mod=%s (elec=%lu mech=%s)",
                  label, ratio_buf, mod_buf,
                  static_cast<unsigned long>(elec_counted),
                  mech_buf);
    Telemetry::log("print", msg);
}

void PolePairCalibrator::update() {
    if (m_state == State::IDLE || m_state == State::DONE || m_state == State::FAIL) {
        return;
    }

    if (!openLoopController().isRunning()) {
        m_state = State::IDLE;
        return;
    }

    sampleEncoderAngle();

    constexpr float CAL_MAX_MOD = 0.50f;
    constexpr float RAMP_STEP = 0.01f;
    constexpr uint32_t RAMP_PERIOD_MS = 50U;
    constexpr float BREAKAWAY_DETECT_CYCLES = 0.15f;
    constexpr float TORQUE_MARGIN = 1.30f;
    constexpr float TARGET_MECH_CYCLES = 1.0f;
    constexpr float MIN_PARTIAL_CYCLES = 0.5f;
    constexpr uint32_t STALL_TIMEOUT_MS = 3000U;
    constexpr uint32_t MAX_COUNT_MS = 120000U;

    const uint32_t now_ms = HAL_GetTick();
    const float mech_cycles = PolePairEstimator::instance().mechanicalCycles();
    const float angle_cycles = std::fabs(encoderCycles());

    if (m_state == State::RAMP) {
        /* Any encoder movement (either direction) resets the stall timer. */
        if (std::fabs(angle_cycles - m_cycles_at_last_move) > 0.01f) {
            m_cycles_at_last_move = angle_cycles;
            m_last_move_ms = now_ms;
        }

        if (!m_breakaway_detected) {
            /* Fast ramp until the shaft moves through ~0.15 cycles. */
            if ((now_ms - m_last_ramp_ms) >= RAMP_PERIOD_MS) {
                m_last_ramp_ms = now_ms;
                m_mod += RAMP_STEP;
                if (m_mod > CAL_MAX_MOD) {
                    m_mod = CAL_MAX_MOD;
                }
                openLoopController().setModulationIndexDirect(m_mod);
            }

            if (angle_cycles >= BREAKAWAY_DETECT_CYCLES) {
                m_breakaway_detected = true;
                m_breakaway_mod = m_mod;
                m_breakaway_mech_cycles = mech_cycles;

                float boosted = m_mod * TORQUE_MARGIN;
                if (boosted > CAL_MAX_MOD) {
                    boosted = CAL_MAX_MOD;
                }
                m_mod = boosted;
                openLoopController().setModulationIndexDirect(m_mod);

                char ba_buf[16];
                fmtFloat3(ba_buf, sizeof(ba_buf), m_breakaway_mod);
                char run_buf[16];
                fmtFloat3(run_buf, sizeof(run_buf), m_mod);
                char msg[80];
                std::snprintf(msg, sizeof(msg),
                              "[CAL PP] breakaway at mod=%s, holding at %s",
                              ba_buf, run_buf);
                Telemetry::log("print", msg);
            }

            if (m_mod >= CAL_MAX_MOD &&
                (now_ms - m_last_move_ms) > STALL_TIMEOUT_MS) {
                m_state = State::FAIL;
                Telemetry::log("print", "[CAL PP] FAIL: encoder did not move");
                openLoopController().stop();
            }
        } else {
            /* Breakaway found.  Wait for the next zero-crossing so the
             * measurement window starts exactly at a robust cycle boundary. */
            if (mech_cycles > m_breakaway_mech_cycles) {
                m_mech_count_start = mech_cycles;
                m_elec_count_start = PWM_GetSPWMElectricalCycles();
                m_count_start_ms = now_ms;
                m_last_move_ms = now_ms;
                m_cycles_at_last_move = angle_cycles;
                m_state = State::COUNT;
                Telemetry::log("print", "[CAL PP] zero crossing, counting one cycle");
            }
        }
    }
    else if (m_state == State::COUNT) {
        if (std::fabs(angle_cycles - m_cycles_at_last_move) > 0.01f) {
            m_cycles_at_last_move = angle_cycles;
            m_last_move_ms = now_ms;
        }

        const float cycles_counted = mech_cycles - m_mech_count_start;

        if ((now_ms - m_last_move_ms) > STALL_TIMEOUT_MS) {
            if (cycles_counted >= MIN_PARTIAL_CYCLES) {
                m_state = State::DONE;
                reportRatio("partial");
            } else {
                m_state = State::FAIL;
                Telemetry::log("print", "[CAL PP] FAIL: encoder stalled during count");
            }
            openLoopController().stop();
            return;
        }

        if ((now_ms - m_count_start_ms) > MAX_COUNT_MS) {
            m_state = State::FAIL;
            Telemetry::log("print", "[CAL PP] FAIL: count took too long");
            openLoopController().stop();
            return;
        }

        if (cycles_counted >= TARGET_MECH_CYCLES) {
            reportRatio("done");
            m_state = State::DONE;
            openLoopController().stop();
        }
    }
}

} // namespace Inverter
