#pragma once

namespace Inverter {

/**
 * @brief Coupled D/Q vector PI controller with circular voltage limit.
 *
 * Ported from PicoFirmware/Source/Utils/Control/VectorPIController.
 * Uses back-calculation anti-windup and an optional output low-pass filter.
 */
class VectorPIController {
public:
    float Kp_ = 0.0f;
    float Ki_ = 0.0f;
    float MaxVoltageLimit_ = 0.0f;  ///< Maximum allowed vector magnitude [V].

    bool EnableOutputFilter_ = false; ///< Toggles the output filter on/off.
    float FilterCutoffHz_ = 1000.0f;  ///< Filter cutoff frequency [Hz].

    VectorPIController() = default;

    void Reset();

    /**
     * @brief Compute decoupled D/Q voltages while respecting a shared circular limit.
     */
    void Update(float id_err, float iq_err,
                float vd_ff, float vq_ff,
                float dt_s,
                float& vd_out, float& vq_out);

private:
    float id_int_ = 0.0f;
    float iq_int_ = 0.0f;

    float vd_filter_state_ = 0.0f;
    float vq_filter_state_ = 0.0f;
};

} // namespace Inverter
