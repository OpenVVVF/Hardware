/**
  ***********************************************************************************
  * @file    FocController.h
  * @date    2026-02-15
  * @brief   Field-Oriented Control using vector_transfs.h.
  *          Outputs Valpha/Vbeta for external modulation (SPWM/SVM).
  ***********************************************************************************
  */

  #pragma once

  #include <cstdint>
  #include "pid_controllers/fp_pid.h"
  #include "space_vector_transfs/vector_transfs.h"
  
  /**
   * @brief Motor and inverter configuration
   */
  struct MotorConfig {
      float _PolePairs_unitless;
      float _Ld_Henry;
      float _Lq_Henry;
      float _FluxLinkage_Wb;
      float _MaxPhaseCurrent_A;
      float _ContinuousPhaseCurrent_A;
      float _MaxDcBusCurrent_A;
      float _MaxRegenCurrent_A;
      float _MaxRpm_unitless;
      float _MinRpm_unitless;
      float _MaxModulation_unitless;
      float _DcBusVoltage_V;
  };
  
  /**
   * @brief Sensor inputs - also used for FOC state to avoid duplication
   */
  struct SensorData {
      float _Iu_A;
      float _Iv_A;
      float _Iw_A;
      float _Idc_A;
      float _EncoderPosition_Rad;
      float _EncoderVelocity_RadPerSec;
      float _DcBusVoltage_V;
  };
  
  /**
   * @brief Current command input
   */
  struct CurrentCommand {
      float _IdCmd_A;                     // D-axis current command (flux)
      float _IqCmd_A;                     // Q-axis current command (torque)
  };
  
  /**
   * @brief FOC output for external modulation
   */
  struct FocOutput {
      float _Valpha_V;                    // Stationary frame alpha voltage
      float _Vbeta_V;                     // Stationary frame beta voltage
      float _Vdc_V;                       // DC bus voltage
      float _ElectricalAngle_Rad;         // For SVM sector calculation
      bool _VoltageLimited;               // True if hit voltage limit
  };

  /**
   * @brief FOC controller
   */
  class FocController {
  public:
      FocController();
      ~FocController() = default;
      
      FocController(const FocController&) = delete;
      FocController& operator=(const FocController&) = delete;
  
      void SetMotorConfig(const MotorConfig& Config);
      MotorConfig GetMotorConfig() const;
      
      void SetDaxisGains(float Kp, float Ki);
      void SetQaxisGains(float Kp, float Ki);
      
      void UpdateSensors(const SensorData& _Sensors);
      
    void CalibrateEncoderOffset(float Voltage_V);

      /**
       * @brief Processes and limits current commands. 
       *        Must be called before UpdateVoltages.
       *        Stores limited targets in internal state.
       */
      void ApplyCurrentLimits(const CurrentCommand& Cmd);
      
      /**
       * @brief Executes PI loops and transforms. 
       *        Uses targets set by ApplyCurrentLimits.
       */
      FocOutput UpdateVoltages();

      void Reset();
  
      // Public state - includes sensor data plus computed values
      SensorData Sensors_;                // Latest sensor inputs
      float _ElectricalAngle_Rad;
      float _ElectricalSpeed_RadPerSec;
      float _SinTheta_unitless;
      float _CosTheta_unitless;
      float _Ialpha_A;
      float _Ibeta_A;
      float _Id_A;
      float _Iq_A;
      
      // Commanded (Target) Currents - Set by ApplyCurrentLimits
      float _IdCommanded_A;
      float _IqCommanded_A;
      
      float _Vd_V;
      float _Vq_V;
      float _Valpha_V;
      float _Vbeta_V;
      bool _PhaseCurrentLimited;
      bool _DcBusCurrentLimited;
  
      float _EncoderOffset_Rad = 0.0f;


  private:
      MotorConfig _Config_;
      
      tPI _DaxisController_;
      tPI _QaxisController_;
      
      tFFClarke _Clarke_;
      tFPark _Park_;
      tIPark _InversePark_;
      
      float _VdFeedforward_V_;
      float _VqFeedforward_V_;

      
      void CalculateDecoupling();
      
      void ApplyVoltageLimiting();
  };