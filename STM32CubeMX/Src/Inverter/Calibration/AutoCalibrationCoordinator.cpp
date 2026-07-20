#include "Inverter/Calibration/AutoCalibrationCoordinator.h"

#include "Inverter/Calibration/PoleCalibrator.h"
#include "Inverter/Calibration/EncoderOffsetCalibrator.h"
#include "Inverter/Calibration/ResistanceCalibrator.h"
#include "Inverter/Calibration/InductanceCalibrator.h"
#include "Inverter/Calibration/FluxLinkageCalibrator.h"
#include "Inverter/Calibration/EncoderCycleCalibrator.h"
#include "Inverter/Calibration/MotorCalibration.h"
#include "Inverter/Control/FocControlManager.h"
#include "Inverter/Control/OpenLoopController.h"
#include "Inverter/Drivers/Sensors/EncoderADC.h"
#include "Inverter/Drivers/Sensors/PoleEstimator.h"
#include "Inverter/Drivers/Storage/MotorConfigStore.h"
#include "Inverter/Telemetry.h"

#include "main.h"
#include <cstdarg>
#include <cmath>

namespace Inverter {

static AutoCalibrationCoordinator s_instance;

AutoCalibrationCoordinator& AutoCalibrationCoordinator::instance() {
    return s_instance;
}

namespace {

/* Conservative power limits suitable for unknown motors. */
static constexpr float MAX_MODULATION = 0.35f;
static constexpr float POLE_TORQUE_MARGIN = 1.30f;
static constexpr float OFFSET_TORQUE_MARGIN = 1.30f;
static constexpr float RES_MAX_CURRENT_A = 30.0f;
static constexpr float RES_OC_LIMIT_A = 100.0f;
static constexpr uint32_t RES_TIMEOUT_MS = 30000U;
static constexpr uint32_t SETTLE_BEFORE_RES_MS = 2000U;

} // namespace

const char* AutoCalibrationCoordinator::stateName() const {
    switch (m_state) {
        case State::IDLE:        return "IDLE";
        case State::POLE:        return "POLE";
        case State::OFFSET:      return "OFFSET";
        case State::SETTLE:      return "SETTLE";
        case State::RESISTANCE:  return "RESISTANCE";
        case State::INDUCTANCE:  return "INDUCTANCE";
        case State::FLUX:        return "FLUX";
        case State::DONE:        return "DONE";
        case State::FAIL:        return "FAIL";
    }
    return "?";
}

void AutoCalibrationCoordinator::enterState(State state) {
    m_state = state;
    m_state_enter_ms = HAL_GetTick();
    Telemetry::printf("[CAL] AUTO: enter %s", stateName());
}

void AutoCalibrationCoordinator::fail(const char* reason_fmt, ...) {
    va_list args;
    va_start(args, reason_fmt);
    Telemetry::vprintf(reason_fmt, args);
    va_end(args);

    /* Make sure any still-active sub-calibrator is stopped and the gate driver
     * is left in a safe, known state. */
    EncoderCycleCalibrator::instance().stop();
    if (poleCalibrator().isActive()) {
        poleCalibrator().stop();
    }
    if (encoderOffsetCalibrator().isActive()) {
        encoderOffsetCalibrator().stop();
    }
    if (resistanceCalibrator().isActive()) {
        resistanceCalibrator().stop();
    }

    encoderADC().learnBounds(false);
    enterState(State::FAIL);
}

void AutoCalibrationCoordinator::stop() {
    if (!isActive()) {
        return;
    }

    EncoderCycleCalibrator::instance().stop();
    if (poleCalibrator().isActive()) {
        poleCalibrator().stop();
    }
    if (encoderOffsetCalibrator().isActive()) {
        encoderOffsetCalibrator().stop();
    }
    if (resistanceCalibrator().isActive()) {
        resistanceCalibrator().stop();
    }

    encoderADC().learnBounds(false);
    Telemetry::printf("[CAL] AUTO: stopped by user");
    enterState(State::IDLE);
}

bool AutoCalibrationCoordinator::start() {
    if (isActive()) {
        Telemetry::printf("[CAL] AUTO: already running");
        return false;
    }

    if (openLoopController().isRunning()) {
        Telemetry::printf("[CAL] AUTO: stop the motor before starting");
        return false;
    }

    if (poleCalibrator().isActive() ||
        encoderOffsetCalibrator().isActive() ||
        resistanceCalibrator().isActive()) {
        Telemetry::printf("[CAL] AUTO: another calibration is active");
        return false;
    }

    m_poles = 0.0f;
    m_encoder_cycles_per_rev = 0.0f;
    m_breakaway_mod = 0.0f;
    m_encoder_offset = 0.0f;
    m_encoder_sign = -1.0f;
    m_r_uv = 0.0f;
    m_r_uw = 0.0f;
    m_r_vw = 0.0f;
    m_r_avg = 0.0f;

    /* Open the encoder hard caps so the sin/cos envelope for this motor is
     * learned during the pole/rotation phase. */
    encoderADC().resetBounds();
    encoderADC().learnBounds(true);

    /* Re-capture current-sensor offsets with the gate driver held in reset and
     * PWM running.  This must happen before every run because a manual `cal`
     * (or a previous failed calibration) can leave bad offsets in place. */
    if (!openLoopController().isInitialized()) {
        if (!openLoopController().init()) {
            Telemetry::printf("[CAL] AUTO: failed to initialize open-loop controller");
            return false;
        }
    }
    if (!openLoopController().recalibrateOffsets()) {
        Telemetry::printf("[CAL] AUTO: current-sensor offset recalibration failed");
        return false;
    }

    /* The pole-cal rotation will also be used to count encoder electrical
     * cycles, eliminating the manual encodercal step. */
    EncoderCycleCalibrator::instance().start();

    Telemetry::printf("[CAL] AUTO: starting motor profile (max_mod=%.2f res_I=%.1f A)",
                      static_cast<double>(MAX_MODULATION),
                      static_cast<double>(RES_MAX_CURRENT_A));

    if (!poleCalibrator().start(MAX_MODULATION, POLE_TORQUE_MARGIN)) {
        EncoderCycleCalibrator::instance().stop();
        Telemetry::printf("[CAL] AUTO: failed to start pole calibration");
        return false;
    }

    enterState(State::POLE);
    return true;
}

void AutoCalibrationCoordinator::update() {
    if (m_state == State::IDLE || m_state == State::DONE || m_state == State::FAIL) {
        return;
    }

    switch (m_state) {
        case State::POLE: {
            PoleCalibrator& pc = poleCalibrator();
            if (pc.isActive()) {
                return; /* still running */
            }

            EncoderCycleCalibrator::instance().stop();

            if (pc.lastPoles() <= 0.0f) {
                fail("[CAL] AUTO: FAIL: pole calibration did not produce a valid pole count");
                return;
            }

            m_poles = pc.lastPoles();
            m_breakaway_mod = pc.lastBreakawayMod();

            const float mech_cycles = PoleEstimator::instance().mechanicalCycles();
            const float enc_cycles = EncoderCycleCalibrator::instance().cycles();
            if (mech_cycles > 0.25f && enc_cycles > 0.0f) {
                m_pole_cal_encoder_cycles_per_rev = enc_cycles / mech_cycles;
                m_encoder_cycles_per_rev = m_pole_cal_encoder_cycles_per_rev;
            } else {
                fail("[CAL] AUTO: FAIL: insufficient rotation for encoder cycle count (mech=%.2f enc=%.2f)",
                     static_cast<double>(mech_cycles),
                     static_cast<double>(enc_cycles));
                return;
            }

            Telemetry::printf("[CAL] AUTO: poles=%.2f enc_cycles/rev=%.2f breakaway_mod=%.3f",
                              static_cast<double>(m_poles),
                              static_cast<double>(m_encoder_cycles_per_rev),
                              static_cast<double>(m_breakaway_mod));

            if (!encoderOffsetCalibrator().start(m_poles, m_encoder_cycles_per_rev,
                                                 m_breakaway_mod, MAX_MODULATION,
                                                 OFFSET_TORQUE_MARGIN)) {
                fail("[CAL] AUTO: FAIL: encoder offset calibration failed to start");
                return;
            }

            enterState(State::OFFSET);
            break;
        }

        case State::OFFSET: {
            EncoderOffsetCalibrator& ec = encoderOffsetCalibrator();
            if (ec.isActive()) {
                return; /* still running */
            }

            if (!ec.isDone()) {
                fail("[CAL] AUTO: FAIL: encoder offset calibration failed");
                return;
            }

            m_encoder_offset = ec.averageOffset();
            m_encoder_sign = static_cast<float>(ec.detectedSign());
            /* Override the pole-cal cycle-count estimate with the more accurate
             * value measured during the controlled offset rotation. */
            m_encoder_cycles_per_rev = ec.measuredEncoderCyclesPerRev();
            Telemetry::printf("[CAL] AUTO: encoder offset=%.3f deg samples=%d sign=%.0f",
                              static_cast<double>(m_encoder_offset),
                              ec.sampleCount(),
                              static_cast<double>(m_encoder_sign));
            Telemetry::printf("[CAL] AUTO: encoder cycles/rev measured=%.3f (pole-cal estimate=%.3f)",
                              static_cast<double>(m_encoder_cycles_per_rev),
                              static_cast<double>(m_pole_cal_encoder_cycles_per_rev));
            Telemetry::printf("[CAL] AUTO: settling %.3f s before resistance cal",
                              static_cast<double>(SETTLE_BEFORE_RES_MS) * 0.001);

            enterState(State::SETTLE);
            break;
        }

        case State::SETTLE: {
            if ((HAL_GetTick() - m_state_enter_ms) < SETTLE_BEFORE_RES_MS) {
                return; /* still settling */
            }

            if (!resistanceCalibrator().startCurrentCtrl(RES_MAX_CURRENT_A,
                                                         ResistanceCalibrator::Pair::UV,
                                                         true, RES_TIMEOUT_MS,
                                                         RES_OC_LIMIT_A)) {
                fail("[CAL] AUTO: FAIL: resistance calibration failed to start");
                return;
            }

            enterState(State::RESISTANCE);
            break;
        }

        case State::RESISTANCE: {
            ResistanceCalibrator& rc = resistanceCalibrator();
            if (rc.isActive()) {
                return; /* still running */
            }

            if (!rc.isDone()) {
                fail("[CAL] AUTO: FAIL: resistance calibration failed");
                return;
            }

            m_r_uv = rc.lastResult(ResistanceCalibrator::Pair::UV);
            m_r_uw = rc.lastResult(ResistanceCalibrator::Pair::UW);
            m_r_vw = rc.lastResult(ResistanceCalibrator::Pair::VW);
            m_r_avg = rc.lastAverage();

            /* Store the calibrated parameters where FOC and telemetry can find
             * them easily. */
            {
                MotorCalibration& mc = motorCalibration();
                mc.pole_count = m_poles;
                mc.encoder_cycles_per_rev = m_encoder_cycles_per_rev;
                mc.encoder_offset_deg = m_encoder_offset;
                mc.encoder_sign = m_encoder_sign;
                mc.r_phase_uv = m_r_uv;
                mc.r_phase_uw = m_r_uw;
                mc.r_phase_vw = m_r_vw;
                mc.r_phase_avg = m_r_avg;
                mc.timestamp_ms = HAL_GetTick();
                mc.valid = true;

                Telemetry::log("motor_poles", mc.pole_count);
                Telemetry::log("motor_enc_cycles", mc.encoder_cycles_per_rev);
                Telemetry::log("motor_enc_offset_deg", mc.encoder_offset_deg);
                Telemetry::log("motor_enc_sign", mc.encoder_sign);
                Telemetry::log("motor_r_phase_uv", mc.r_phase_uv);
                Telemetry::log("motor_r_phase_uw", mc.r_phase_uw);
                Telemetry::log("motor_r_phase_vw", mc.r_phase_vw);
                Telemetry::log("motor_r_phase_avg", mc.r_phase_avg);
            }

            /* The fresh calibration replaces the offset the runtime adjustment
             * was tuned against; drop the stale delta so the next FOC start
             * uses exactly the measured offset. */
            focControlManager().resetEncoderOffsetAdjustment();

            encoderADC().learnBounds(false);

            /* Measure dq inductances (biased-AC injection).  The FRAM save
             * happens after this stage so it includes the inductances. */
            Telemetry::printf("[CAL] AUTO: enter INDUCTANCE");
            m_inductance_ran = inductanceCalibrator().start(RES_MAX_CURRENT_A);
            if (!m_inductance_ran) {
                Telemetry::printf("[CAL] AUTO: WARNING: inductance cal failed to start; "
                                  "continuing without it");
            }
            enterState(State::INDUCTANCE);
            break;
        }

        case State::INDUCTANCE: {
            InductanceCalibrator& ic = inductanceCalibrator();
            if (ic.isActive()) {
                return; /* still running */
            }

            if (m_inductance_ran && ic.isDone() && ic.lastLd() > 0.0f) {
                MotorCalibration& mc = motorCalibration();
                mc.ld_henry = ic.lastLd();
                mc.lq_henry = ic.lastLq();
                Telemetry::printf("[CAL] AUTO: Ld(0)=%.1f uH Lq(0)=%.1f uH",
                                  static_cast<double>(mc.ld_henry * 1.0e6),
                                  static_cast<double>(mc.lq_henry * 1.0e6));
            } else {
                Telemetry::printf("[CAL] AUTO: WARNING: inductance calibration "
                                  "incomplete; Ld/Lq left unset");
            }

            /* Measure the PM flux linkage (back-EMF speed sweep).  The FRAM
             * save happens after this stage so it includes everything. */
            Telemetry::printf("[CAL] AUTO: enter FLUX");
            m_flux_ran = fluxLinkageCalibrator().start(RES_MAX_CURRENT_A * 0.67f);
            if (!m_flux_ran) {
                Telemetry::printf("[CAL] AUTO: WARNING: flux cal failed to start; "
                                  "continuing without it");
            }
            enterState(State::FLUX);
            break;
        }

        case State::FLUX: {
            FluxLinkageCalibrator& fc = fluxLinkageCalibrator();
            if (fc.isActive()) {
                return; /* still running */
            }

            if (m_flux_ran && fc.isDone() && fc.lastFlux() > 0.0f) {
                Telemetry::printf("[CAL] AUTO: flux linkage=%.5f Wb",
                                  static_cast<double>(fc.lastFlux()));
            } else {
                Telemetry::printf("[CAL] AUTO: WARNING: flux calibration "
                                  "incomplete; psi_m left unset");
            }

            /* Persist the freshly measured profile so FOC can run after a
             * reboot without re-running motorcal. */
            if (MotorConfigStore::saveFromRuntime()) {
                Telemetry::printf("[CAL] AUTO: motor config saved to FRAM");
            }

            enterState(State::DONE);

            /* One clear, self-contained summary block. */
            Telemetry::printf("[CAL] AUTO: ===========================================");
            Telemetry::printf("[CAL] AUTO: MOTOR PROFILE COMPLETE");
            Telemetry::printf("[CAL] AUTO:   poles                = %.2f", static_cast<double>(m_poles));
            Telemetry::printf("[CAL] AUTO:   encoder cycles/rev   = %.2f", static_cast<double>(m_encoder_cycles_per_rev));
            Telemetry::printf("[CAL] AUTO:   encoder offset       = %.3f deg", static_cast<double>(m_encoder_offset));
            Telemetry::printf("[CAL] AUTO:   encoder sign         = %.0f", static_cast<double>(m_encoder_sign));
            Telemetry::printf("[CAL] AUTO:   R_phase (UV/UW/VW)   = %.4f / %.4f / %.4f ohm",
                              static_cast<double>(m_r_uv),
                              static_cast<double>(m_r_uw),
                              static_cast<double>(m_r_vw));
            Telemetry::printf("[CAL] AUTO:   R_phase_avg          = %.4f ohm  (%.2f mohm)",
                              static_cast<double>(m_r_avg),
                              static_cast<double>(m_r_avg * 1000.0f));
            Telemetry::printf("[CAL] AUTO:   R_ll_avg             = %.4f ohm  (%.2f mohm)",
                              static_cast<double>(m_r_avg * 2.0f),
                              static_cast<double>(m_r_avg * 2000.0f));
            if (motorCalibration().ld_henry > 0.0f) {
                Telemetry::printf("[CAL] AUTO:   Ld / Lq (0 A)      = %.1f / %.1f uH",
                                  static_cast<double>(motorCalibration().ld_henry * 1.0e6),
                                  static_cast<double>(motorCalibration().lq_henry * 1.0e6));
            }
            if (motorCalibration().flux_linkage_wb > 0.0f) {
                Telemetry::printf("[CAL] AUTO:   flux linkage       = %.5f Wb",
                                  static_cast<double>(motorCalibration().flux_linkage_wb));
            }
            Telemetry::printf("[CAL] AUTO: ===========================================");
            break;
        }

        case State::IDLE:
        case State::DONE:
        case State::FAIL:
        default:
            break;
    }
}

AutoCalibrationCoordinator& autoCalibrationCoordinator() {
    return AutoCalibrationCoordinator::instance();
}

} // namespace Inverter
