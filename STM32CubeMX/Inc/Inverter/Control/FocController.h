#pragma once

#include "Inverter/Control/MotorParameters.h"
#include "Inverter/Control/VectorPIController.h"

namespace Inverter {

struct FocConfig {
    float Kp_D = 0.03f;
    float Ki_D = 10.0f;
    float Kp_Q = 0.03f;
    float Ki_Q = 10.0f;
    float SoftVoltageLimit_V = 0.0f; ///< 0 -> use Vdc-derived limit.
    float MaxPhaseCurrent_A = 40.0f;
    float MaxModulation = 0.9f;
};

struct FocInputs {
    float iu_a;
    float iv_a;
    float iw_a;
    float vdc_v;
    float encoder_angle_rad;         ///< Mechanical angle, 0..2pi.
    float encoder_velocity_rad_per_s;///< Mechanical velocity (may be 0 if estimated internally).
};

struct FocSetpoints {
    float id_a = 0.0f;
    float iq_a = 0.0f;
    float vd_ff_v = 0.0f;
    float vq_ff_v = 0.0f;
};

struct FocOutputs {
    float valpha_v;
    float vbeta_v;
    float vd_v;
    float vq_v;
    float id_a;
    float iq_a;
    float electrical_angle_rad;
    float electrical_speed_rad_per_s;
};

/**
 * @brief Field-oriented current controller.
 *
 * Ported from PicoFirmware/Source/Switching/Control/Schemas/FOC.
 * Runs one current-control step per call to update().
 */
class FocController {
public:
    FocController() = default;

    void ApplyConfig(const FocConfig& config);
    void SetMotorParameters(const MotorParameters& params);

    void Reset();

    /**
     * @brief Execute one FOC step.
     * @param in     Sensor inputs.
     * @param set    Current setpoints.
     * @param dt_s   Controller time step [s].
     * @return Stationary-frame voltage vector and internal states.
     */
    FocOutputs Update(const FocInputs& in, const FocSetpoints& set, float dt_s);

    // --- Telemetry state (mirrors Pico variable names) ---
    float ElectricalAngle_Rad = 0.0f;
    float ElectricalSpeed_RadPerSec = 0.0f;
    float SinTheta = 0.0f;
    float CosTheta = 1.0f;

    float Ialpha_A = 0.0f;
    float Ibeta_A = 0.0f;
    float Id_A = 0.0f;
    float Iq_A = 0.0f;

    float IdCommanded_A = 0.0f;
    float IqCommanded_A = 0.0f;

    float Vd_V = 0.0f;
    float Vq_V = 0.0f;
    float Valpha_V = 0.0f;
    float Vbeta_V = 0.0f;

    bool PhaseCurrentLimited = false;
    bool DcBusCurrentLimited = false;

    const VectorPIController& currentLoop() const { return current_loop_; }

private:
    void CalculateDecoupling();

    FocConfig config_;
    MotorParameters motor_;
    VectorPIController current_loop_;

    float vd_decoupling_ff_v_ = 0.0f;
    float vq_decoupling_ff_v_ = 0.0f;

    // Velocity estimator state.
    float prev_mech_angle_rad_ = 0.0f;
    float mech_velocity_rad_per_s_ = 0.0f;
    bool velocity_init_ = false;

    static constexpr float VEL_FILTER_ALPHA = 0.2f;
};

} // namespace Inverter
