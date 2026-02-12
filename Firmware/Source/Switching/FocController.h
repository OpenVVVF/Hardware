#pragma once
#include <cstdint>
#include <cmath>
#include "ThreePhasePwmBridge.h"

// ===============================
// Math helpers
// ===============================
struct AlphaBeta { float a=0, b=0; };
struct DQ { float d=0, q=0; };

inline AlphaBeta clarke(float ia, float ib, float ic) {
    // Assumes ia+ib+ic ≈ 0; uses ia, ib; ic is still accepted for diagnostics.
    // alpha = ia
    // beta  = (ia + 2*ib)/sqrt(3)
    constexpr float inv_sqrt3 = 0.5773502691896258f;
    AlphaBeta ab;
    ab.a = ia;
    ab.b = (ia + 2.0f * ib) * inv_sqrt3;
    (void)ic;
    return ab;
}

inline DQ park(const AlphaBeta& ab, float theta_e) {
    float c = cosf(theta_e);
    float s = sinf(theta_e);
    DQ dq;
    dq.d =  ab.a * c + ab.b * s;
    dq.q = -ab.a * s + ab.b * c;
    return dq;
}

inline AlphaBeta invPark(const DQ& dq, float theta_e) {
    float c = cosf(theta_e);
    float s = sinf(theta_e);
    AlphaBeta ab;
    ab.a = dq.d * c - dq.q * s;
    ab.b = dq.d * s + dq.q * c;
    return ab;
}

inline float clampf(float x, float lo, float hi) {
    return (x < lo) ? lo : (x > hi) ? hi : x;
}

// ===============================
// PI controller with anti-windup
// ===============================
class PIController {
public:
    struct Gains { float kp=0.0f, ki=0.0f; };

    void setGains(float kp, float ki) { g_.kp = kp; g_.ki = ki; }
    void reset(float integrator = 0.0f) { i_ = integrator; }

    // "u" output clamped; integrator uses back-calculation style clamping
    float update(float err, float dt, float u_min, float u_max) {
        float p = g_.kp * err;
        float u_unsat = p + i_;
        float u = clampf(u_unsat, u_min, u_max);

        // anti-windup: only integrate if not saturated or if pushing out of saturation
        float sat_err = (u - u_unsat);
        // simple back-calc (tunable via factor = 1 here)
        i_ += (g_.ki * err + sat_err / (dt > 1e-6f ? dt : 1e-6f)) * dt;

        return u;
    }

private:
    Gains g_{};
    float i_ = 0.0f;
};

// ===============================
// Measurements / Commands / Outputs
// ===============================
struct FocMeasurements {
    // Timing
    float dt = 0.0f;            // seconds (your control period)

    // Electrical angle and speed (electrical radians, electrical rad/s)
    float theta_e = 0.0f;       // required
    float omega_e = 0.0f;       // optional (set 0 if unknown)

    // Phase currents (A). Provide all three if you have them; controller uses ia/ib.
    float ia = 0.0f;
    float ib = 0.0f;
    float ic = 0.0f;

    // Bus voltage (V) (required for proper voltage limiting)
    float vbus = 0.0f;

    // Optional extras you might want to pass through (not required by control law)
    float temperature_C = 0.0f;
    float throttle = 0.0f;
    uint32_t fault_flags = 0;
};

struct FocCommand {
    // Current setpoints (A)
    float id_ref = 0.0f;   // typically 0 for surface PMSM
    float iq_ref = 0.0f;   // torque-producing current

    // Optional direct voltage mode (bypass PI), if you want (e.g. debug/voltage control)
    bool  voltage_mode = false;
    float vd_ref = 0.0f;   // V
    float vq_ref = 0.0f;   // V
};

struct FocOutputs {
    // Estimated currents in DQ
    float id = 0.0f;
    float iq = 0.0f;

    // Voltage commands in DQ (V)
    float vd = 0.0f;
    float vq = 0.0f;

    // Voltage commands in AlphaBeta (V)
    float v_alpha = 0.0f;
    float v_beta  = 0.0f;

    // Final phase voltages (V) actually sent to bridge
    float vu = 0.0f;
    float vv = 0.0f;
    float vw = 0.0f;

    // Utilization and limiting info
    float v_mag = 0.0f;       // magnitude requested (V)
    float v_max = 0.0f;       // magnitude allowed (V)
    bool  voltage_limited = false;
};

// ===============================
// SVPWM modulator (alpha-beta volts -> phase volts)
// ===============================
class Svpwm {
public:
    // Convert alpha-beta voltage (V) into phase voltages (V) with zero-sequence injection.
    // This returns phase voltages centered within [0..Vbus] after injection, then shifted to
    // be phase-to-midpoint voltages for ThreePhasePwmBridge::setPhaseVoltagesVolts().
    static void alphaBetaToPhaseVoltages(float v_alpha, float v_beta, float vbus,
                                         float& vu, float& vv, float& vw) {
        // Convert alpha-beta to 3-phase (phase-to-neutral) voltages (no injection yet)
        // Using inverse Clarke:
        // vu = v_alpha
        // vv = -0.5*v_alpha + (sqrt(3)/2)*v_beta
        // vw = -0.5*v_alpha - (sqrt(3)/2)*v_beta
        constexpr float sqrt3_over_2 = 0.8660254037844386f;

        float a = v_alpha;
        float b = (-0.5f * v_alpha) + (sqrt3_over_2 * v_beta);
        float c = (-0.5f * v_alpha) - (sqrt3_over_2 * v_beta);

        // SVPWM zero-sequence injection: shift by -(max+min)/2
        float v_max = fmaxf(a, fmaxf(b, c));
        float v_min = fminf(a, fminf(b, c));
        float v0 = -0.5f * (v_max + v_min);

        a += v0; b += v0; c += v0;

        // These a/b/c are still phase-to-neutral-like commands; for bridge we want
        // phase-to-midpoint voltages (±Vbus/2). That’s exactly what a,b,c represent if
        // we ensure they stay within ±Vbus/2 (handled by limiter upstream).
        vu = a;
        vv = b;
        vw = c;

        (void)vbus;
    }
};

// ===============================
// FOC Controller
// ===============================
class FocController {
public:
    struct MotorParams {
        // Electrical parameters for decoupling/feedforward
        float R = 0.0f;      // phase resistance (ohm)
        float Ld = 0.0f;     // d-axis inductance (H)
        float Lq = 0.0f;     // q-axis inductance (H)
        float flux = 0.0f;   // PM flux linkage (V*s/rad) (optional, for back-EMF term)
    };

    struct Limits {
        float iq_max = 50.0f;         // A
        float id_min = -50.0f;        // A (field weakening)
        float id_max =  50.0f;        // A
        float v_utilization = 0.577f; // max |Vab| magnitude as fraction of Vbus
        // 0.577 ~= Vbus/sqrt(3) for SVPWM linear region in alpha-beta magnitude terms.
    };

    struct Config {
        MotorParams motor{};
        Limits limits{};

        // Current loop PI gains
        PIController::Gains id_gains{ .kp = 0.0f, .ki = 0.0f };
        PIController::Gains iq_gains{ .kp = 0.0f, .ki = 0.0f };

        // If true, adds decoupling/feedforward terms (needs omega_e, params)
        bool enable_decoupling = true;

        // If you want a phase-current polarity swap / wiring flip, do it here:
        bool invert_currents = false;
    };

    explicit FocController(const Config& cfg) : cfg_(cfg) {
        id_pi_.setGains(cfg_.id_gains.kp, cfg_.id_gains.ki);
        iq_pi_.setGains(cfg_.iq_gains.kp, cfg_.iq_gains.ki);
    }

    void reset() {
        id_pi_.reset();
        iq_pi_.reset();
        out_ = {};
    }

    // Main control step: computes phase voltages and writes to the bridge.
    // You pass measurements + command each tick.
    void step(ThreePhasePwmBridge& bridge, const FocMeasurements& m, const FocCommand& cmd) {
        out_ = {}; // refresh outputs each step

        if (m.dt <= 0.0f || m.vbus <= 0.5f) {
            // Not enough info to do safe control
            bridge.setPhaseVoltagesPU(0,0,0);
            return;
        }

        // Clamp references
        float id_ref = clampf(cmd.id_ref, cfg_.limits.id_min, cfg_.limits.id_max);
        float iq_ref = clampf(cmd.iq_ref, -cfg_.limits.iq_max, cfg_.limits.iq_max);

        // Current sign handling
        float ia = cfg_.invert_currents ? -m.ia : m.ia;
        float ib = cfg_.invert_currents ? -m.ib : m.ib;
        float ic = cfg_.invert_currents ? -m.ic : m.ic;

        // Measure currents in dq
        AlphaBeta i_ab = clarke(ia, ib, ic);
        DQ i_dq = park(i_ab, m.theta_e);

        out_.id = i_dq.d;
        out_.iq = i_dq.q;

        // Voltage limits
        // Conservative limit: |Vab| <= utilization * Vbus
        float v_max = cfg_.limits.v_utilization * m.vbus;
        out_.v_max = v_max;

        float vd = 0.0f, vq = 0.0f;

        if (cmd.voltage_mode) {
            vd = cmd.vd_ref;
            vq = cmd.vq_ref;
        } else {
            // PI current regulators (outputs are volts)
            float ed = (id_ref - i_dq.d);
            float eq = (iq_ref - i_dq.q);

            // We’ll clamp each axis to +/- v_max; then do vector limiting after feedforward.
            vd = id_pi_.update(ed, m.dt, -v_max, v_max);
            vq = iq_pi_.update(eq, m.dt, -v_max, v_max);

            if (cfg_.enable_decoupling) {
                // Standard PMSM decoupling terms:
                // vd += -omega * Lq * iq
                // vq +=  omega * (Ld * id + flux)
                float w = m.omega_e;
                vd += -w * cfg_.motor.Lq * i_dq.q;
                vq +=  w * (cfg_.motor.Ld * i_dq.d + cfg_.motor.flux);
                // Optional resistive feedforward:
                vd += cfg_.motor.R * i_dq.d;
                vq += cfg_.motor.R * i_dq.q;
            }
        }

        // Vector magnitude limit in dq (simple scaling)
        float v_mag = sqrtf(vd*vd + vq*vq);
        out_.v_mag = v_mag;

        if (v_mag > v_max && v_mag > 1e-6f) {
            float s = v_max / v_mag;
            vd *= s; vq *= s;
            out_.voltage_limited = true;
        }

        out_.vd = vd;
        out_.vq = vq;

        // Convert to alpha-beta
        AlphaBeta v_ab = invPark({vd, vq}, m.theta_e);
        out_.v_alpha = v_ab.a;
        out_.v_beta  = v_ab.b;

        // SVPWM -> phase voltages
        float vu, vv, vw;
        Svpwm::alphaBetaToPhaseVoltages(v_ab.a, v_ab.b, m.vbus, vu, vv, vw);

        // Final safety: clamp phase voltages to +/- Vbus/2
        float half = 0.5f * m.vbus;
        vu = clampf(vu, -half, half);
        vv = clampf(vv, -half, half);
        vw = clampf(vw, -half, half);

        out_.vu = vu; out_.vv = vv; out_.vw = vw;

        // Drive the bridge (volts + vbus -> per-unit internally)
        bridge.setPhaseVoltagesVolts(vu, vv, vw, m.vbus);
    }

    const FocOutputs& outputs() const { return out_; }

private:
    Config cfg_;
    PIController id_pi_;
    PIController iq_pi_;
    FocOutputs out_{};
};
