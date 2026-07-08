#include "Inverter/Control/FocController.h"
#include "Inverter/Control/Math/FocMath.h"

#include <algorithm>
#include <cmath>

namespace Inverter {

void FocController::ApplyConfig(const FocConfig& config) {
    config_ = config;
    current_loop_.Kp_ = config_.Kp_Q;
    current_loop_.Ki_ = config_.Ki_Q;
}

void FocController::SetMotorParameters(const MotorParameters& params) {
    motor_ = params;
}

void FocController::Reset() {
    current_loop_.Reset();

    ElectricalAngle_Rad = 0.0f;
    ElectricalSpeed_RadPerSec = 0.0f;
    SinTheta = 0.0f;
    CosTheta = 1.0f;

    Ialpha_A = 0.0f;
    Ibeta_A = 0.0f;
    Id_A = 0.0f;
    Iq_A = 0.0f;

    IdCommanded_A = 0.0f;
    IqCommanded_A = 0.0f;

    Vd_V = 0.0f;
    Vq_V = 0.0f;
    Valpha_V = 0.0f;
    Vbeta_V = 0.0f;

    PhaseCurrentLimited = false;
    DcBusCurrentLimited = false;

    vd_decoupling_ff_v_ = 0.0f;
    vq_decoupling_ff_v_ = 0.0f;

    velocity_init_ = false;
    mech_velocity_rad_per_s_ = 0.0f;
}

void FocController::CalculateDecoupling() {
    // Use commanded currents for decoupling to avoid positive feedback at speed.
    vd_decoupling_ff_v_ = -ElectricalSpeed_RadPerSec * motor_.lq_henry * IqCommanded_A;
    vq_decoupling_ff_v_ = (ElectricalSpeed_RadPerSec * motor_.ld_henry * IdCommanded_A) +
                          (ElectricalSpeed_RadPerSec * motor_.flux_linkage_wb);
}

FocOutputs FocController::Update(const FocInputs& in, const FocSetpoints& set, float dt_s) {
    FocOutputs out{};

    // --- 1. Dynamic voltage limit ---
    if (config_.SoftVoltageLimit_V > 0.001f) {
        current_loop_.MaxVoltageLimit_ = config_.SoftVoltageLimit_V;
    } else {
        current_loop_.MaxVoltageLimit_ = in.vdc_v * 0.5f * config_.MaxModulation;
    }

    // --- 2. Sensor processing and transforms ---
    // The encoder ADC returns the angle within one sin/cos cycle.  If the
    // encoder magnet has multiple cycles per mechanical revolution, scale the
    // angle so that one mechanical revolution maps to pole_pairs electrical
    // revolutions.
    float encoder_cycle_angle = wrapAngle2Pi(in.encoder_angle_rad);
    float elec_scale = (motor_.encoder_cycles_per_rev > 1e-6f)
                           ? (motor_.pole_pairs / motor_.encoder_cycles_per_rev)
                           : motor_.pole_pairs;
    /* The calibration stores offset as the encoder mechanical angle when the
     * stator field points at U-high.  The encoder direction sign is stored in
     * the calibration so the rotor electrical angle follows the encoder correctly:
     *   theta_elec = offset_elec + sign * encoder_elec
     * sign = -1 reproduces the original "reversed encoder" behaviour. */
    ElectricalAngle_Rad = wrapAngle2Pi(motor_.encoder_offset_rad +
                                       motor_.encoder_sign * encoder_cycle_angle * elec_scale);
    SinTheta = sinf(ElectricalAngle_Rad);
    CosTheta = cosf(ElectricalAngle_Rad);

    // Estimate mechanical velocity from angle if caller didn't provide it.
    if (!velocity_init_) {
        prev_mech_angle_rad_ = encoder_cycle_angle;
        mech_velocity_rad_per_s_ = in.encoder_velocity_rad_per_s;
        velocity_init_ = true;
    } else {
        float delta = wrapAnglePi(encoder_cycle_angle - prev_mech_angle_rad_);
        float raw_vel = delta / dt_s;
        // Compensate the raw delta for encoder cycles per rev so the velocity
        // is mechanical, not per encoder cycle.
        raw_vel *= (motor_.encoder_cycles_per_rev > 1e-6f)
                       ? (1.0f / motor_.encoder_cycles_per_rev)
                       : 1.0f;
        mech_velocity_rad_per_s_ += VEL_FILTER_ALPHA * (raw_vel - mech_velocity_rad_per_s_);
        if (std::fabs(in.encoder_velocity_rad_per_s) > 1e-6f) {
            // Prefer caller-provided velocity if available.
            mech_velocity_rad_per_s_ = in.encoder_velocity_rad_per_s;
        }
    }
    prev_mech_angle_rad_ = encoder_cycle_angle;
    ElectricalSpeed_RadPerSec = mech_velocity_rad_per_s_ * motor_.pole_pairs;

    // Forward Clarke.
    clarkeAbcToAlphaBeta(in.iu_a, in.iv_a, in.iw_a, Ialpha_A, Ibeta_A);

    // Forward Park.
    parkAlphaBetaToDq(Ialpha_A, Ibeta_A, SinTheta, CosTheta, Id_A, Iq_A);

    // --- 3. Command saturation ---
    IdCommanded_A = std::clamp(set.id_a, -config_.MaxPhaseCurrent_A, config_.MaxPhaseCurrent_A);
    IqCommanded_A = std::clamp(set.iq_a, -config_.MaxPhaseCurrent_A, config_.MaxPhaseCurrent_A);

    // Circular current limit.
    float mag_sq = IdCommanded_A * IdCommanded_A + IqCommanded_A * IqCommanded_A;
    float max_sq = config_.MaxPhaseCurrent_A * config_.MaxPhaseCurrent_A;
    PhaseCurrentLimited = (mag_sq > max_sq);
    if (PhaseCurrentLimited) {
        float scale = config_.MaxPhaseCurrent_A / std::sqrt(mag_sq);
        IdCommanded_A *= scale;
        IqCommanded_A *= scale;
    }

    // --- 4. Vector PI control ---
    CalculateDecoupling();

    float total_vd_ff = vd_decoupling_ff_v_ + set.vd_ff_v;
    float total_vq_ff = vq_decoupling_ff_v_ + set.vq_ff_v;

    float id_err = IdCommanded_A - Id_A;
    float iq_err = IqCommanded_A - Iq_A;

    current_loop_.Update(id_err, iq_err, total_vd_ff, total_vq_ff, dt_s, Vd_V, Vq_V);

    float v_mag = std::sqrt(Vd_V * Vd_V + Vq_V * Vq_V);
    DcBusCurrentLimited = (v_mag >= current_loop_.MaxVoltageLimit_ * 0.99f);

    // --- 5. Inverse Park ---
    inverseParkDqToAlphaBeta(Vd_V, Vq_V, SinTheta, CosTheta, Valpha_V, Vbeta_V);

    // --- 6. Populate output ---
    out.valpha_v = Valpha_V;
    out.vbeta_v = Vbeta_V;
    out.vd_v = Vd_V;
    out.vq_v = Vq_V;
    out.id_a = Id_A;
    out.iq_a = Iq_A;
    out.electrical_angle_rad = ElectricalAngle_Rad;
    out.electrical_speed_rad_per_s = ElectricalSpeed_RadPerSec;

    return out;
}

} // namespace Inverter
