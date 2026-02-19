/**
***********************************************************************************
* @file    FOC.h
* @date    2026-02-19
* @brief   Field-Oriented Control (FOC) Implementation.
* Inherits from ControlScheme. Uses closed-loop current feedback and 
* Park/Clarke transforms to synthesize optimal stator voltages.
***********************************************************************************
*/

#pragma once

#include <cstdint>
#include "BaseSchema.h"
#include "pid_controllers/fp_pid.h"
#include "space_vector_transfs/vector_transfs.h"
#include "../../VectorPIController.h"

/**
* @brief Specific configuration for FOC loop tuning.
* Inherits influence bounds from ControlCommonConfig.
*/
struct FocConfig : public ControlCommonConfig {
    float _Kp_D_axis = 0.0f;       ///< Proportional gain for Flux axis
    float _Ki_D_axis = 0.0f;       ///< Integral gain for Flux axis
    float _Kp_Q_axis = 0.0f;       ///< Proportional gain for Torque axis
    float _Ki_Q_axis = 0.0f;       ///< Integral gain for Torque axis
    float _SoftVoltageLimit_V = 0.0f; ///< Override for maximum phase voltage (0 to use HW default)
};

/**
* @brief FOC scheme controller.
*/
class FocController : public ControlScheme {
    public:
    FocController();
    ~FocController() = default;

    FocController(const FocController&) = delete;
    FocController& operator=(const FocController&) = delete;

    /**
    * @brief Applies loop tuning and soft limits.
    * @param _Config The FOC-specific configuration struct.
    */
    void ApplyConfig(FocConfig _Config);

    /**
    * @brief Executes the high-speed FOC PI loops and spatial transforms.
    * @param _Sensors Current feedback and encoder telemetry.
    * @param _Cmd Commanded Id/Iq targets and feedforward.
    * @param _dt_S Loop delta time in seconds.
    * @return ModulationInput (Stationary frame voltages and electrical angle).
    */
    ModulationInput Update(const SensorData& _Sensors, const DriveCommand& _Cmd, float _dt_S) override;
    
    /**
    * @brief Clears PI integrators and state memory for bumpless transfers.
    */
    void Reset() override;

    // --- State Variables (Public for Telemetry) ---
    SensorData Sensors_;  
    float _ElectricalAngle_Rad;
    float _ElectricalSpeed_RadPerSec;
    float _SinTheta_unitless;
    float _CosTheta_unitless;
    
    // Measured Currents
    float _Ialpha_A;
    float _Ibeta_A;
    float _Id_A;
    float _Iq_A;
    
    // Target Currents
    float _IdCommanded_A;
    float _IqCommanded_A;
    
    // Output Voltages
    float _Vd_V;
    float _Vq_V;
    float _Valpha_V;
    float _Vbeta_V;
    
    bool _PhaseCurrentLimited;
    bool _DcBusCurrentLimited;
    
    float _EncoderOffset_Rad = 0.0f;
    VectorPIController _CurrentLoop;

    // Debug tracking variables
    float i_alpha; 
    float i_beta; 
    float i_d; 
    float i_q;

    private:
    FocConfig SpecificConfig_;
    
    tFFClarke _Clarke_;
    tFPark _Park_;
    tIPark _InversePark_;
    
    float _VdDecouplingFF_V_;
    float _VqDecouplingFF_V_;

    /**
    * @brief Pre-calculates BEMF and cross-coupling voltage to assist PI controllers.
    */
    void CalculateDecoupling();
};