// /**
//  ***********************************************************************************
//  * @file    FocController.h
//  * @date    2026-02-15
//  * @brief   Field-Oriented Control using vector_transfs.h.
//  *          Outputs Valpha/Vbeta for external modulation (SPWM/SVM).
//  ***********************************************************************************
//  */

// #pragma once

// #include <cstdint>

// #include "pid_controllers/fp_pid.h"
// #include "space_vector_transfs/vector_transfs.h"

// #include "VectorPIController.h"
// #include "Control/ControlInput.h"

// #include "Modulation/ModulationInput.h"


// /**
//  * @brief FOC controller
//  */
// class FocController {
//    public:
//     FocController();
//     ~FocController() = default;

//     FocController(const FocController&) = delete;
//     FocController& operator=(const FocController&) = delete;

//     void SetMotorConfig(const MotorConfig& Config);
//     MotorConfig GetMotorConfig() const;

//     void SetDaxisGains(float Kp, float Ki);
//     void SetQaxisGains(float Kp, float Ki);

//     void CalibrateEncoderOffset(float Voltage_V);

//     /**
//      * @brief Processes and limits current commands.
//      *        Must be called before UpdateVoltages.
//      *        Stores limited targets in internal state.
//      */
//     void ApplyCurrentLimits(const CurrentCommand& Cmd);

//     /**
//      * @brief Executes PI loops and transforms.
//      *        Uses targets set by ApplyCurrentLimits.
//      */
//     ModulationInput UpdateVoltages(float dt_S);

//     /**
//      * @brief internal limiter for the PID controller, will soft limit here.
//      * clamps kI windup too.
//      */
//     void SetVoltageLimit(float _Voltage_V);

//     void UpdateSensors(const SensorData& Sensors);

//     void Reset();

//     // Public state - includes sensor data plus computed values
//     SensorData Sensors_;  // Latest sensor inputs
//     float _ElectricalAngle_Rad;
//     float _ElectricalSpeed_RadPerSec;
//     float _SinTheta_unitless;
//     float _CosTheta_unitless;
//     float _Ialpha_A;
//     float _Ibeta_A;
//     float _Id_A;
//     float _Iq_A;

//     // Commanded (Target) Currents - Set by ApplyCurrentLimits
//     float _IdCommanded_A;
//     float _IqCommanded_A;

//     float _Vd_V;
//     float _Vq_V;
//     float _Valpha_V;
//     float _Vbeta_V;
//     bool _PhaseCurrentLimited;
//     bool _DcBusCurrentLimited;

//     float _EncoderOffset_Rad = 0.0f;

//     VectorPIController _CurrentLoop;

//     // debug
//     float i_alpha;
//     float i_beta;

//     float i_d;
//     float i_q;

//    private:
//     MotorConfig _Config_;

//     tFFClarke _Clarke_;
//     tFPark _Park_;
//     tIPark _InversePark_;

//     float _VdFeedforward_V_;
//     float _VqFeedforward_V_;

//     void CalculateDecoupling();

// };