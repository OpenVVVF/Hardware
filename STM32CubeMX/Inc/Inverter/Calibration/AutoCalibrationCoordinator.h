#pragma once

#include <cstdint>

namespace Inverter {

/**
 * @brief Fully automatic motor profiling routine.
 *
 * Sequentially runs the existing calibrators to determine:
 *   1. Motor pole count and encoder cycles per revolution (shared rotation).
 *   2. Encoder offset.
 *   3. Phase-to-phase resistance.
 *
 * The routine takes no motor-specific constants.  All limits are conservative
 * to keep power dissipation low:
 *   - maximum modulation index during rotation: 0.35
 *   - resistance calibration current: 10 A (20 A overcurrent abort)
 */
class AutoCalibrationCoordinator {
public:
    AutoCalibrationCoordinator() = default;

    /**
     * @brief Start the automatic profiling routine.
     *
     * The motor must be stopped and no other calibration may be active.
     */
    bool start();

    /**
     * @brief Abort a running routine and return to idle.
     */
    void stop();

    /**
     * @brief Non-blocking state-machine update.  Call at ~100 Hz from the main
     * loop.
     */
    void update();

    /** @brief True while profiling is in progress. */
    bool isActive() const {
        return m_state != State::IDLE && m_state != State::DONE &&
               m_state != State::FAIL;
    }

    /** @brief True if the last routine finished successfully. */
    bool isDone() const { return m_state == State::DONE; }

    /** @brief True if the last routine failed. */
    bool isFailed() const { return m_state == State::FAIL; }

    /** @brief Human-readable status string for telemetry/shell. */
    const char* stateName() const;

    float lastPoles() const { return m_poles; }
    float lastEncoderCyclesPerRev() const { return m_encoder_cycles_per_rev; }
    float lastBreakawayMod() const { return m_breakaway_mod; }
    float lastEncoderOffset() const { return m_encoder_offset; }
    float lastResistanceUv() const { return m_r_uv; }
    float lastResistanceUw() const { return m_r_uw; }
    float lastResistanceVw() const { return m_r_vw; }
    float lastResistanceAverage() const { return m_r_avg; }

    static AutoCalibrationCoordinator& instance();

private:
    enum class State {
        IDLE,
        POLE,
        OFFSET,
        SETTLE,
        RESISTANCE,
        INDUCTANCE,
        FLUX,
        DONE,
        FAIL
    };

    void enterState(State state);
    void fail(const char* reason_fmt, ...);

    State m_state = State::IDLE;
    uint32_t m_state_enter_ms = 0;

    float m_poles = 0.0f;
    float m_encoder_cycles_per_rev = 0.0f;
    float m_pole_cal_encoder_cycles_per_rev = 0.0f;
    float m_breakaway_mod = 0.0f;
    float m_encoder_offset = 0.0f;
    float m_encoder_sign = -1.0f;
    float m_r_uv = 0.0f;
    float m_r_uw = 0.0f;
    float m_r_vw = 0.0f;
    float m_r_avg = 0.0f;
    bool m_inductance_ran = false;
    bool m_flux_ran = false;
};

AutoCalibrationCoordinator& autoCalibrationCoordinator();

} // namespace Inverter
