/**
  ***********************************************************************************
  * @file    FocController.cpp
  * @date    2026-02-15
  * @brief   Field-Oriented Control implementation.
  ***********************************************************************************
  */

  #include "FOC.h"
  #include <cmath>

  #include "Sensors/MeasurementSystem.h"


  FocController::FocController()
  : _ElectricalAngle_Rad(0.0f),
    _ElectricalSpeed_RadPerSec(0.0f),
    _SinTheta_unitless(0.0f),
    _CosTheta_unitless(1.0f),
    _Ialpha_A(0.0f),
    _Ibeta_A(0.0f),
    _Id_A(0.0f),
    _Iq_A(0.0f),
    _IdCommanded_A(0.0f),
    _IqCommanded_A(0.0f),
    _Vd_V(0.0f),
    _Vq_V(0.0f),
    _Valpha_V(0.0f),
    _Vbeta_V(0.0f),
    _PhaseCurrentLimited(false),
    _DcBusCurrentLimited(false),
    _VdFeedforward_V_(0.0f),
    _VqFeedforward_V_(0.0f)
{


//   _DaxisController_ = {};
//   _DaxisController_.fDtSec = 0.001f;
//   _DaxisController_.m_calc = tPI_calc;
//   _DaxisController_.m_rst = tPI_rst;


    // // Maybe works... idfk
    // int TargetBandwidth = 2000; // hz
    // float MotorInductance_H =  0.000080f;
    // float Resistance_Ohm = 0.025;

    // float Bandwidth = TargetBandwidth * 2 * M_PI;

    // float kP = Bandwidth * MotorInductance_H;
    // // float kI = (Resistance_Ohm / MotorInductance_H) * Bandwidth * MotorInductance_H;
    // float kI = 0;

//   _DaxisController_.fKp = kP;
//   _DaxisController_.fKi = kI; 
//   _DaxisController_.fLowOutLim = -1.0f; // volts (Eventually make this equal to the voltage of dc bus)
//   _DaxisController_.fUpOutLim = 1.0f; // volts (Eventually make this equal to the voltage of dc bus)


//   _QaxisController_ = {};
//   _QaxisController_.fDtSec = 0.001f;
//   _QaxisController_.m_calc = tPI_calc;
//   _QaxisController_.m_rst = tPI_rst;

//   _QaxisController_.fKp = kP;
//   _QaxisController_.fKi = kI;
//   _QaxisController_.fLowOutLim = -1.0f; // volts (Eventually make this equal to the voltage of dc bus)
//   _QaxisController_.fUpOutLim = 1.0f; // volts (Eventually make this equal to the voltage of dc bus) 


  _Clarke_ = {};
  _Clarke_.m_abc2albe = tFFClarke_abc2albe;
  _Park_ = {};
  _Park_.m_albe2dq = tFPark_albe2dq;
  _InversePark_ = {};
  _InversePark_.m_dq2albe = tIPark_dq2albe;

  Sensors_ = {};
  _CurrentLoop = {};
}
  
  void FocController::SetMotorConfig(const MotorConfig& Config) {
      _Config_ = Config;
      _CurrentLoop.MaxVoltageLimit = 10.0f;//_Config_._DcBusVoltage_V * 0.5f * _Config_._MaxModulation_unitless;
  }
  
  MotorConfig FocController::GetMotorConfig() const {
      return _Config_;
  }
  
  void FocController::SetDaxisGains(float Kp, float Ki) {
    _CurrentLoop.Kp = Kp;  // Assuming D and Q share gains for an SPM
    _CurrentLoop.Ki = Ki;
}

void FocController::SetQaxisGains(float Kp, float Ki) {
    _CurrentLoop.Kp = Kp;
    _CurrentLoop.Ki = Ki;
}
  void FocController::Reset() {
      // Reset PID controllers

      _CurrentLoop.Reset();
    //   tPI_rst(&_DaxisController_);
    //   tPI_rst(&_QaxisController_);
      
      // Reset state variables
      _ElectricalAngle_Rad = 0.0f;
      _ElectricalSpeed_RadPerSec = 0.0f;
      _SinTheta_unitless = 0.0f;
      _CosTheta_unitless = 1.0f;
      _Ialpha_A = 0.0f;
      _Ibeta_A = 0.0f;
      _Id_A = 0.0f;
      _Iq_A = 0.0f;
      _IdCommanded_A = 0.0f;
      _IqCommanded_A = 0.0f;
      _Vd_V = 0.0f;
      _Vq_V = 0.0f;
      _Valpha_V = 0.0f;
      _Vbeta_V = 0.0f;
      _PhaseCurrentLimited = false;
      _DcBusCurrentLimited = false;
      
      _VdFeedforward_V_ = 0.0f;
      _VqFeedforward_V_ = 0.0f;
  }
  
    void FocController::CalibrateEncoderOffset(float Voltage_V) {
        // 1. Override the electrical angle to 0 (Align to Phase A)
        _ElectricalAngle_Rad = 0.0f;

        // 2. Precompute sin/cos for this angle (sin(0)=0, cos(0)=1)
        _SinTheta_unitless = 0.0f;
        _CosTheta_unitless = 1.0f;

        // 3. Manually set Voltage vectors to lock rotor to D-axis
        // Vd = Voltage, Vq = 0 (No torque, just flux alignment)
        _Vd_V = Voltage_V; 
        _Vq_V = 0.0f;

        // 4. Apply voltage limiting just in case
        // ApplyVoltageLimiting();

        // 5. Inverse Park (D/Q -> Alpha/Beta)
        _InversePark_.fD = _Vd_V;
        _InversePark_.fQ = _Vq_V;
        _InversePark_.fSinAng = _SinTheta_unitless;
        _InversePark_.fCosAng = _CosTheta_unitless;
        _InversePark_.m_dq2albe(&_InversePark_);

        _Valpha_V = _InversePark_.fAl;
        _Vbeta_V = _InversePark_.fBe;

        // 6. IMPORTANT: Zero out the PID integrators so they don't wind up
        // while we are manually controlling voltage.
        // tPI_rst(&_DaxisController_);
        // tPI_rst(&_QaxisController_);
    }

  void FocController::UpdateSensors(const SensorData& Sensors) {
      // Store sensor data in public state
      Sensors_ = Sensors;
      
      // Compute electrical angle and speed
    _ElectricalAngle_Rad = fmodf(Sensors._EncoderPosition_Rad * _Config_._PolePairs_unitless + _EncoderOffset_Rad, 2.0f * 3.1415926535);
    if (_ElectricalAngle_Rad < 0.0f) _ElectricalAngle_Rad += 2.0f * 3.1415926535;


    //   _ElectricalAngle_Rad = Sensors._EncoderPosition_Rad * _Config_._PolePairs_unitless;
      _ElectricalSpeed_RadPerSec = Sensors._EncoderVelocity_RadPerSec * _Config_._PolePairs_unitless;
      
      // Precompute sin/cos for Park transforms
      _SinTheta_unitless = sinf(_ElectricalAngle_Rad);
      _CosTheta_unitless = cosf(_ElectricalAngle_Rad);
      
      // Forward Clarke: ABC -> Alpha/Beta
      _Clarke_.fA = Sensors._Iu_A;
      _Clarke_.fB = Sensors._Iv_A;
      _Clarke_.fC = Sensors._Iw_A;
      _Clarke_.m_abc2albe(&_Clarke_);
      
      _Ialpha_A = _Clarke_.fAl;
      i_alpha = _Ialpha_A;
      _Ibeta_A = _Clarke_.fBe;
      i_beta = _Ibeta_A;

      // Forward Park: Alpha/Beta -> D/Q
      _Park_.fAl = _Ialpha_A;
      _Park_.fBe = _Ibeta_A;
      _Park_.fSinAng = _SinTheta_unitless;
      _Park_.fCosAng = _CosTheta_unitless;
      _Park_.m_albe2dq(&_Park_);
      
      _Id_A = _Park_.fD;
      _Iq_A = _Park_.fQ;
      i_d = _Id_A;
      i_q = _Iq_A;
  }
  
  void FocController::ApplyCurrentLimits(const CurrentCommand& Cmd) {
      // Store D-axis command (pass-through for now, usually 0 for SPM)
      _IdCommanded_A = Cmd._IdCmd_A;
      
      // Limit Iq (torque) based on thermal and DC bus constraints
      float IqMax = _Config_._MaxPhaseCurrent_A;
      float IqMin = -_Config_._MaxPhaseCurrent_A;
      
      // Apply limits
      float IqCmdLimited = fmaxf(IqMin, fminf(IqMax, Cmd._IqCmd_A));
      
      // Update status flag
      if (IqCmdLimited != Cmd._IqCmd_A) {
          _PhaseCurrentLimited = true;
      } else {
          _PhaseCurrentLimited = false;
      }
      
      // Store limited Q-axis command into internal state
      _IqCommanded_A = IqCmdLimited;
  }
  
  void FocController::CalculateDecoupling() {
      // Feedforward decoupling terms
      // Vd_ff = -omega_e * Lq * Iq
      // Vq_ff = omega_e * Ld * Id + omega_e * lambda_pm
      
      _VdFeedforward_V_ = -_ElectricalSpeed_RadPerSec * _Config_._Lq_Henry * _Iq_A;
      _VqFeedforward_V_ = _ElectricalSpeed_RadPerSec * _Config_._Ld_Henry * _Id_A 
                          + _ElectricalSpeed_RadPerSec * _Config_._FluxLinkage_Wb;
  }
  
//   void FocController::ApplyVoltageLimiting() {
//       // Circle limitation in d-q frame
//       float Vd = _Vd_V;
//       float Vq = _Vq_V;
//       float Vmag = sqrtf(Vd * Vd + Vq * Vq);
      
//       float Vmax = 0.5f * _Config_._DcBusVoltage_V * _Config_._MaxModulation_unitless;

//       // Note: sqrt(3)/3 for peak phase voltage from DC bus, or adjust based on your modulation
      
//       if (Vmag > Vmax) {
//           float Scale = Vmax / Vmag;
//           _Vd_V *= Scale;
//           _Vq_V *= Scale;
//           _DcBusCurrentLimited = true;  // Reusing flag for voltage saturation
//       } else {
//           _DcBusCurrentLimited = false;
//       }
//   }
  
  FocOutput FocController::UpdateVoltages(float dt_S) {
    FocOutput Output = {0};
    
    // 1. Calculate feedforward terms
    CalculateDecoupling();
    
    // 2. Calculate errors
    // _IdCommanded_A = 0.0f;
    // _IqCommanded_A = 20.0f;
    float Id_err = _IdCommanded_A - _Id_A;
    float Iq_err = _IqCommanded_A - _Iq_A;

    // 3. Execute Coupled Vector PI Loop 
    // (Assuming your dt is fixed at 0.001f for now, pass it from Core 1 later!)
    _CurrentLoop.Update(Id_err, Iq_err, _VdFeedforward_V_, _VqFeedforward_V_, dt_S, _Vd_V, _Vq_V);
    
    // Check if we hit the limit for telemetry
    float v_mag = sqrtf(_Vd_V * _Vd_V + _Vq_V * _Vq_V);
    _DcBusCurrentLimited = (v_mag >= _CurrentLoop.MaxVoltageLimit * 0.99f);
    
    // 4. Inverse Park: D/Q -> Alpha/Beta
    _InversePark_.fD = _Vd_V;
    _InversePark_.fQ = _Vq_V;
    _InversePark_.fSinAng = _SinTheta_unitless;
    _InversePark_.fCosAng = _CosTheta_unitless;
    _InversePark_.m_dq2albe(&_InversePark_);
    
    _Valpha_V = _InversePark_.fAl;
    _Vbeta_V = _InversePark_.fBe;
    
    // 5. Populate output
    Output._Valpha_V = _Valpha_V;
    Output._Vbeta_V = _Vbeta_V;
    Output._Vdc_V = _Config_._DcBusVoltage_V;
    Output._ElectricalAngle_Rad = _ElectricalAngle_Rad;
    Output._VoltageLimited = _DcBusCurrentLimited; 
    
    return Output;
}