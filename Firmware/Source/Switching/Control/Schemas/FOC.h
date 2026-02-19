/**
 ***********************************************************************************
 * @file    FOC.h
 * @date    2026-02-19
 * @brief   Field-Oriented Control. Inherits from ControlScheme.
 ***********************************************************************************
 */

 #pragma once

 #include <cstdint>
 #include "BaseSchema.h"
 #include "pid_controllers/fp_pid.h"
 #include "space_vector_transfs/vector_transfs.h"
 #include "../../VectorPIController.h"
 
 struct FocConfig : public ControlCommonConfig {
     float _Kp_D_axis = 0.0f;
     float _Ki_D_axis = 0.0f;
     float _Kp_Q_axis = 0.0f;
     float _Ki_Q_axis = 0.0f;
     float _SoftVoltageLimit_V = 0.0f; 
 };
 
 class FocController : public ControlScheme {
    public:
     FocController();
     ~FocController() = default;
 
     FocController(const FocController&) = delete;
     FocController& operator=(const FocController&) = delete;
 
     void ApplyConfig(FocConfig _Config);
     void CalibrateEncoderOffset(float Voltage_V);
 
     ModulationInput Update(const SensorData& _Sensors, const DriveCommand& _Cmd, float _dt_S) override;
     void Reset() override;
 
     SensorData Sensors_;  
     float _ElectricalAngle_Rad;
     float _ElectricalSpeed_RadPerSec;
     float _SinTheta_unitless;
     float _CosTheta_unitless;
     float _Ialpha_A;
     float _Ibeta_A;
     float _Id_A;
     float _Iq_A;
     float _IdCommanded_A;
     float _IqCommanded_A;
     float _Vd_V;
     float _Vq_V;
     float _Valpha_V;
     float _Vbeta_V;
     bool _PhaseCurrentLimited;
     bool _DcBusCurrentLimited;
     float _EncoderOffset_Rad = 0.0f;
     VectorPIController _CurrentLoop;
 
     float i_alpha; float i_beta; float i_d; float i_q;
 
    private:
     FocConfig SpecificConfig_;
     tFFClarke _Clarke_;
     tFPark _Park_;
     tIPark _InversePark_;
     float _VdDecouplingFF_V_;
     float _VqDecouplingFF_V_;
 
     void CalculateDecoupling();
 };