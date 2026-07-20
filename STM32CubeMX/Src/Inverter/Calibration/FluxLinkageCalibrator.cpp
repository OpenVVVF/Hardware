#include "Inverter/Calibration/FluxLinkageCalibrator.h"
#include "Inverter/Calibration/MotorCalibration.h"
#include "Inverter/Calibration/PoleCalibrator.h"
#include "Inverter/Calibration/EncoderOffsetCalibrator.h"
#include "Inverter/Calibration/ResistanceCalibrator.h"
#include "Inverter/Calibration/InductanceCalibrator.h"
#include "Inverter/Control/FaultManager.h"
#include "Inverter/Control/FocControlManager.h"
#include "Inverter/Control/OpenLoopController.h"
#include "Inverter/Drivers/Sensors/DcLinkVoltageSensor.h"
#include "Inverter/Drivers/Sensors/EncoderADC.h"
#include "Inverter/Telemetry.h"

#include "main.h"

#include <cstdarg>
#include <cstring>
#include <cmath>
#include <cstdio>
#include <algorithm>

namespace Inverter {

namespace {

FluxLinkageCalibrator s_instance;

constexpr uint32_t RAMP_MS           = 12000U;
constexpr uint32_t SAMPLE_EVERY_MS   = 50U;
constexpr float    MIN_RPM           = 40.0f;  /* back-EMF well above noise */

} // namespace

FluxLinkageCalibrator& FluxLinkageCalibrator::instance() {
    return s_instance;
}

FluxLinkageCalibrator& fluxLinkageCalibrator() {
    return FluxLinkageCalibrator::instance();
}

bool FluxLinkageCalibrator::isActive() const {
    return m_state != State::IDLE && m_state != State::DONE && m_state != State::FAIL;
}

const char* FluxLinkageCalibrator::stateName() const {
    switch (m_state) {
        case State::IDLE:   return "IDLE";
        case State::RAMP:   return "RAMP";
        case State::FINISH: return "FINISH";
        case State::DONE:   return "DONE";
        case State::FAIL:   return "FAIL";
    }
    return "?";
}

int FluxLinkageCalibrator::pointCount() const { return m_n_points; }
float FluxLinkageCalibrator::iqPoint(int i) const {
    return (i >= 0 && i < m_n_points && i < MAX_POINTS) ? m_iq[i] : 0.0f;
}
float FluxLinkageCalibrator::rpmPoint(int i) const {
    return (i >= 0 && i < m_n_points && i < MAX_POINTS) ? m_rpm[i] : 0.0f;
}
float FluxLinkageCalibrator::fluxPoint(int i) const {
    return (i >= 0 && i < m_n_points && i < MAX_POINTS) ? m_flux[i] : 0.0f;
}

void FluxLinkageCalibrator::fail(const char* fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    std::vsnprintf(m_fail_reason, sizeof(m_fail_reason), fmt, ap);
    va_end(ap);
    Telemetry::printf("[CAL] FLUX: FAIL: %s", m_fail_reason);

    if (focControlManager().isRunning()) {
        focControlManager().setId(0.0f);
        focControlManager().setIq(0.0f);
    }
    focControlManager().stop();
    enterState(State::FAIL);
}

void FluxLinkageCalibrator::enterState(State s) {
    m_state = s;
    m_state_enter_ms = HAL_GetTick();
}

bool FluxLinkageCalibrator::start(float max_iq_a) {
    if (isActive()) {
        Telemetry::printf("[CAL] FLUX: already running");
        return false;
    }
    if (openLoopController().isRunning() || focControlManager().isRunning()) {
        Telemetry::printf("[CAL] FLUX: stop the motor first");
        return false;
    }
    if (poleCalibrator().isActive() || encoderOffsetCalibrator().isActive() ||
        resistanceCalibrator().isActive() || inductanceCalibrator().isActive()) {
        Telemetry::printf("[CAL] FLUX: another calibration is active");
        return false;
    }
    const MotorCalibration& mc = motorCalibration();
    if (!mc.valid || mc.r_phase_avg <= 0.0f || mc.pole_count < 2.0f) {
        Telemetry::printf("[CAL] FLUX: need a valid motor calibration first (run motorcal)");
        return false;
    }
    if (FaultManager::instance().isSeverityActive(FaultSeverity::Critical) ||
        FaultManager::instance().isSeverityActive(FaultSeverity::High)) {
        Telemetry::printf("[CAL] FLUX: active Critical/High faults, cannot start");
        return false;
    }
    if (dcLinkVoltageSensor().voltage() < 10.0f) {
        Telemetry::printf("[CAL] FLUX: DC bus too low (%.1f V)",
                          static_cast<double>(dcLinkVoltageSensor().voltage()));
        return false;
    }
    if (max_iq_a < 4.0f || max_iq_a > 200.0f) {
        Telemetry::printf("[CAL] FLUX: bad parameters");
        return false;
    }

    m_max_iq_a = max_iq_a;
    m_flux_wb = 0.0f;
    m_n_points = 0;
    m_nsamp = 0;
    m_last_sample_ms = 0;
    for (int i = 0; i < MAX_POINTS; ++i) {
        m_iq[i] = m_rpm[i] = m_flux[i] = 0.0f;
    }
    m_fail_reason[0] = '\0';

    /* Runs inside motorcal's FLUX stage, so bypass the cal-active guard. */
    if (!focControlManager().start(0.0f, 0.0f, true)) {
        Telemetry::printf("[CAL] FLUX: failed to start FOC");
        return false;
    }

    Telemetry::printf("[CAL] FLUX: back-EMF flux linkage: iq ramp to %.1f A, R=%.4f ohm",
                      static_cast<double>(m_max_iq_a),
                      static_cast<double>(mc.r_phase_avg));

    focControlManager().setId(0.0f);
    focControlManager().setIq(0.0f);
    m_ramp_start_ms = HAL_GetTick();
    enterState(State::RAMP);
    return true;
}

void FluxLinkageCalibrator::stop() {
    if (!isActive()) {
        return;
    }
    focControlManager().stop();
    Telemetry::printf("[CAL] FLUX: stopped");
    enterState(State::IDLE);
}

void FluxLinkageCalibrator::update() {
    if (!isActive()) {
        return;
    }

    const uint32_t now = HAL_GetTick();
    const MotorCalibration& mc = motorCalibration();

    switch (m_state) {
        case State::RAMP: {
            /* Ramp iq from 0 to max over RAMP_MS, sampling psi_m along the
             * way: every instant of the sweep is a measurement at a
             * different speed, and using the MEASURED iq keeps the result
             * insensitive to ramp rate (L * di/dt is negligible here). */
            const uint32_t elapsed = now - m_ramp_start_ms;
            const float ramp_frac = (elapsed >= RAMP_MS) ? 1.0f
                : static_cast<float>(elapsed) / static_cast<float>(RAMP_MS);
            focControlManager().setIq(ramp_frac * m_max_iq_a);

            const float rpm = encoderADC().rpmMech();
            const float vq = focControlManager().controller().Vq_V;
            const float vd = focControlManager().controller().Vd_V;
            const float vmag = std::sqrt(vd * vd + vq * vq);
            const float ceiling = 0.97f * 0.5f * dcLinkVoltageSensor().voltage() * 0.9f;
            if (vmag >= ceiling && std::fabs(rpm) > MIN_RPM) {
                Telemetry::printf("[CAL] FLUX: hit voltage ceiling at %.0f RPM, %d samples kept",
                                  static_cast<double>(rpm), m_nsamp);
                enterState(State::FINISH);
                return;
            }

            if (std::fabs(rpm) >= MIN_RPM && m_nsamp < MAX_SAMPLES &&
                (now - m_last_sample_ms) >= SAMPLE_EVERY_MS) {
                m_last_sample_ms = now;
                const float pole_pairs = mc.pole_count * 0.5f;
                const float omega_e = rpm * (3.14159265358979323846f / 30.0f) * pole_pairs;
                const float iq = focControlManager().controller().Iq_A;
                if (std::fabs(omega_e) > 1.0f) {
                    m_samp_psi[m_nsamp] = (vq - mc.r_phase_avg * iq) / omega_e;
                    m_samp_rpm[m_nsamp] = rpm;
                    ++m_nsamp;
                }
            }

            if (elapsed >= RAMP_MS) {
                enterState(State::FINISH);
            }
            return;
        }

        case State::FINISH: {
            if (m_nsamp < 3) {
                fail("only %d valid samples (motor never reached %.0f RPM?)",
                     m_nsamp, static_cast<double>(MIN_RPM));
                return;
            }

            /* Joint fit for (psi_m, V_off) over the whole sweep:
             *     vq - R*iq = omega_e * psi_m + V_off
             * Least squares with x = omega_e, y = (vq - R*iq) = psi_i*omega_e.
             * Separating the VSI voltage offset keeps low-speed samples from
             * inflating the estimate. */
            double sx = 0.0, sy = 0.0, sxx = 0.0, sxy = 0.0;
            const float pole_pairs = mc.pole_count * 0.5f;
            for (int s = 0; s < m_nsamp; ++s) {
                const double we = static_cast<double>(m_samp_rpm[s]) *
                                  (3.14159265358979323846 / 30.0) * pole_pairs;
                const double y = static_cast<double>(m_samp_psi[s]) * we;
                sx += we; sy += y; sxx += we * we; sxy += we * y;
            }
            const double n = static_cast<double>(m_nsamp);
            const double denom = n * sxx - sx * sx;
            if (std::fabs(denom) < 1.0e-9) {
                fail("degenerate fit (all samples at one speed)");
                return;
            }
            const double slope = (n * sxy - sx * sy) / denom;
            const double v_off = (sy - slope * sx) / n;

            /* Median as an un-modeled cross-check. */
            float vals[MAX_SAMPLES];
            std::memcpy(vals, m_samp_psi, sizeof(float) * m_nsamp);
            std::sort(vals, vals + m_nsamp);
            const float median = (m_nsamp % 2 == 1) ? vals[m_nsamp / 2]
                : 0.5f * (vals[m_nsamp / 2 - 1] + vals[m_nsamp / 2]);

            if (slope <= 0.0) {
                fail("nonphysical fit: psi_m=%.5f Wb", slope);
                return;
            }
            m_flux_wb = static_cast<float>(slope);

            /* Publish into the runtime calibration. */
            motorCalibration().flux_linkage_wb = m_flux_wb;

            focControlManager().stop();

            Telemetry::printf("[CAL] FLUX: ========================================");
            Telemetry::printf("[CAL] FLUX: FLUX LINKAGE COMPLETE");
            Telemetry::printf("[CAL] FLUX:   psi_m  = %.5f Wb (LS fit, %d samples)",
                              slope, m_nsamp);
            Telemetry::printf("[CAL] FLUX:   V_off  = %.3f V (VSI offset, fitted)", v_off);
            Telemetry::printf("[CAL] FLUX:   median = %.5f Wb (uncorrected cross-check)",
                              static_cast<double>(median));
            if (std::fabs(median - m_flux_wb) / m_flux_wb > 0.15f) {
                Telemetry::printf("[CAL] FLUX:   NOTE: median and fit differ; fit preferred "
                                  "(median is offset-biased at low speed)");
            }
            Telemetry::printf("[CAL] FLUX: ========================================");

            /* Point table for the shell: median per RPM decade bin. */
            int filled = 0;
            for (int lo = 50; lo < 50 + MAX_POINTS * 50 && filled < MAX_POINTS; lo += 50) {
                float bin_vals[MAX_SAMPLES];
                int nb = 0;
                for (int s = 0; s < m_nsamp; ++s) {
                    if (m_samp_rpm[s] >= static_cast<float>(lo) &&
                        m_samp_rpm[s] < static_cast<float>(lo + 50)) {
                        bin_vals[nb++] = m_samp_psi[s];
                    }
                }
                if (nb == 0) continue;
                std::sort(bin_vals, bin_vals + nb);
                const float med = (nb % 2 == 1) ? bin_vals[nb / 2]
                    : 0.5f * (bin_vals[nb / 2 - 1] + bin_vals[nb / 2]);
                m_iq[filled] = 0.0f;
                m_rpm[filled] = static_cast<float>(lo + 25);
                m_flux[filled] = med;
                Telemetry::printf("[CAL] FLUX:   %3d-%3d RPM: psi_m=%.5f Wb (%d samples)",
                                  lo, lo + 50, static_cast<double>(med), nb);
                ++filled;
            }
            m_n_points = filled;

            Telemetry::log("flux_wb", m_flux_wb);
            enterState(State::DONE);
            return;
        }

        default:
            return;
    }
}

} // namespace Inverter
