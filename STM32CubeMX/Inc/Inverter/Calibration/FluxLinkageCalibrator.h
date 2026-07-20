#pragma once

#include <cstdint>

namespace Inverter {

/**
 * @brief Offline PM flux linkage (psi_m) estimation from back-EMF.
 *
 * Spins the motor under closed-loop FOC with id = 0 at a ladder of q-axis
 * current steps.  At each steady speed point the q-axis voltage equation
 *     vq = R * iq + omega_e * psi_m      (id = 0, steady state)
 * gives the flux linkage directly:
 *     psi_m = (vq - R * iq) / omega_e.
 *
 * This is the standard commissioning back-EMF measurement: valid at any
 * speed, on any PMSM, with the stator-resistance correction (from motorcal)
 * handling the loaded case.  Points that never reach a usable speed are
 * skipped; the headline is the median of the valid points.
 */
class FluxLinkageCalibrator {
public:
    static constexpr int MAX_POINTS = 6;

    /**
     * @brief Start the calibration.
     *
     * Requires: valid motor calibration (pole count, encoder offset/sign and
     * phase resistance), DC bus present, no active Critical/High faults,
     * motor stopped, FOC not running.
     *
     * @param max_iq_a  Highest q-axis current step [A].
     */
    bool start(float max_iq_a = 20.0f);
    void stop();

    /** Main-loop state machine; call every iteration. */
    void update();

    bool isActive() const;
    bool isDone() const { return m_state == State::DONE; }
    const char* stateName() const;

    /* Results. */
    float lastFlux() const { return m_flux_wb; }
    int pointCount() const;
    float iqPoint(int i) const;
    float rpmPoint(int i) const;
    float fluxPoint(int i) const;

    static FluxLinkageCalibrator& instance();

private:
    enum class State { IDLE, RAMP, FINISH, DONE, FAIL };

    void fail(const char* reason_fmt, ...);
    void enterState(State s);

    State m_state = State::IDLE;
    uint32_t m_state_enter_ms = 0;
    char m_fail_reason[96] = {0};

    /* Configuration. */
    float m_max_iq_a = 20.0f;

    /* Per-sample collection during the iq ramp (downsampled). */
    static constexpr int MAX_SAMPLES = 260;
    uint32_t m_ramp_start_ms = 0;
    uint32_t m_last_sample_ms = 0;
    int m_nsamp = 0;
    float m_samp_psi[MAX_SAMPLES] = {};
    float m_samp_rpm[MAX_SAMPLES] = {};

    /* Results. */
    float m_flux_wb = 0.0f;
    int m_n_points = 0;
    float m_iq[MAX_POINTS] = {};
    float m_rpm[MAX_POINTS] = {};
    float m_flux[MAX_POINTS] = {};
};

FluxLinkageCalibrator& fluxLinkageCalibrator();

} // namespace Inverter
