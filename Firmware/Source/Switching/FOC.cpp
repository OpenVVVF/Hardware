/**
  ***********************************************************************************
  * @file    FocController.cpp
  * @date    2026-02-15
  * @brief   Field-Oriented Control implementation.
  ***********************************************************************************
  */

  #include "FocController.h"
  #include <cmath>
  
  FocController::FocController() 
      : _VdFeedforward_V_(0.0f),
        _VqFeedforward_V_(0.0f) {
      
      // Initialize PID controllers with defaults
      _DaxisController_ = PI_DEFAULTS;
      _QaxisController_ = PI_DEFAULTS;
      
      // Initialize transform modules
      _Clarke_ = FF_CLARKE_DEFAULTS;
      _Park_ = F_PARK_DEFAULTS;
      _InversePark_ = I_PARK_DEFAULTS;
      
      // Zero state
      State_ = {};
  }
  
  void FocController::SetMotorConfig(const MotorConfig& Config) {
      _Config_ = Config;
  }
  
  MotorConfig FocController::GetMotorConfig() const {
      return _Config_;
  }
  
  void FocController::SetDaxisGains(float Kp, float Ki) {
      _DaxisController_.fKp = Kp;
      _DaxisController_.fKi = Ki;
  }
  
  void FocController::SetQaxisGains(float Kp, float Ki) {
      _QaxisController_.fKp = Kp;
      _QaxisController_.fKi = Ki;
  }
  
  void FocController::Reset() {
      // Reset PID controllers
      tPI_rst(&_DaxisController_);
      tPI_rst(&_QaxisController_);
      
      // Reset state
      State_ = {};
      _VdFeedforward_V_ = 0.0f;
      _VqFeedforward_V_ = 0.0f;
  }
  
  void FocController::UpdateSensors(const SensorData& Sensors) {
      // Compute electrical angle and speed
      State_._ElectricalAngle_Rad = Sensors._EncoderPosition_Rad * _Config_._PolePairs_unitless;
      State_._ElectricalSpeed_RadPerSec = Sensors._EncoderVelocity_RadPerSec * _Config_._PolePairs_unitless;
      
      // Precompute sin/cos for Park transforms
      State_._SinTheta_unitless = sinf(State_._ElectricalAngle_Rad);
      State_._CosTheta_unitless = cosf(State_._ElectricalAngle_Rad);
      
      // Forward Clarke: ABC -> Alpha/Beta
      _Clarke_.fA = Sensors._Iu_A;
      _Clarke_.fB = Sensors._Iv_A;
      _Clarke_.fC = Sensors._Iw_A;
      _Clarke_.m_abc2albe(&_Clarke_);
      
      State_._Ialpha_A = _Clarke_.fAl;
      State_._Ibeta_A = _Clarke_.fBe;
      
      // Forward Park: Alpha/Beta -> D/Q
      _Park_.fAl = State_._Ialpha_A;
      _Park_.fBe = State_._Ibeta_A;
      _Park_.fSinAng = State_._SinTheta_unitless;
      _Park_.fCosAng = State_._CosTheta_unitless;
      _Park_.m_albe2dq(&_Park_);
      
      State_._Id_A = _Park_.fD;
      State_._Iq_A = _Park_.fQ;
  }
  
  void FocController::ApplyCurrentLimits(CurrentReference& Ref) {
      // Limit Id (flux) - usually 0 for SPM, negative for flux weakening
      // Not implementing full MTPA/flux weakening here, just hard limits
      
      // Limit Iq (torque) based on thermal and DC bus constraints
      float IqMax = _Config_._MaxPhaseCurrent_A;
      float IqMin = -_Config_._MaxPhaseCurrent_A;
      
      // Check DC bus current approximation: Idc ≈ 3/2 * (Id*cos + Iq*sin) for rough limit
      // Simplified: just check magnitude for now
      float IqRefLimited = fmaxf(IqMin, fminf(IqMax, Ref._IqRef_A));
      
      if (IqRefLimited != Ref._IqRef_A) {
          State_._PhaseCurrentLimited = true;
      } else {
          State_._PhaseCurrentLimited = false;
      }
      
      Ref._IqRef_A = IqRefLimited;
  }
  
  void FocController::CalculateDecoupling() {
      // Feedforward decoupling terms
      // Vd_ff = -omega_e * Lq * Iq
      // Vq_ff = omega_e * Ld * Id + omega_e * lambda_pm
      
      _VdFeedforward_V_ = -State_._ElectricalSpeed_RadPerSec * _Config_._Lq_Henry * State_._Iq_A;
      _VqFeedforward_V_ = State_._ElectricalSpeed_RadPerSec * _Config_._Ld_Henry * State_._Id_A 
                         + State_._ElectricalSpeed_RadPerSec * _Config_._FluxLinkage_Wb;
  }
  
  void FocController::ApplyVoltageLimiting() {
      // Circle limitation in d-q frame
      float Vd = State_._Vd_V;
      float Vq = State_._Vq_V;
      float Vmag = sqrtf(Vd * Vd + Vq * Vq);
      
      float Vmax = _Config_._DcBusVoltage_V * _Config_._MaxModulation_unitless / sqrtf(3.0f);
      // Note: sqrt(3)/3 for peak phase voltage from DC bus, or adjust based on your modulation
      
      if (Vmag > Vmax) {
          float Scale = Vmax / Vmag;
          State_._Vd_V *= Scale;
          State_._Vq_V *= Scale;
          State_._DcBusCurrentLimited = true;  // Reusing flag for voltage saturation
      } else {
          State_._DcBusCurrentLimited = false;
      }
  }
  
  FocOutput FocController::UpdateVoltages(const CurrentReference& Ref) {
      FocOutput Output = {};
      
      // Make mutable copy for limiting
      CurrentReference RefLimited = Ref;
      
      // Apply current limits
      ApplyCurrentLimits(RefLimited);
      
      // Calculate feedforward terms
      CalculateDecoupling();
      
      // D-axis PI controller
      _DaxisController_.fIn = RefLimited._IdRef_A - State_._Id_A;
      _DaxisController_.m_calc(&_DaxisController_);
      State_._Vd_V = _DaxisController_.fOut + _VdFeedforward_V_;
      
      // Q-axis PI controller
      _QaxisController_.fIn = RefLimited._IqRef_A - State_._Iq_A;
      _QaxisController_.m_calc(&_QaxisController_);
      State_._Vq_V = _QaxisController_.fOut + _VqFeedforward_V_;
      
      // Apply voltage limiting (circle limitation)
      ApplyVoltageLimiting();
      
      // Inverse Park: D/Q -> Alpha/Beta
      _InversePark_.fD = State_._Vd_V;
      _InversePark_.fQ = State_._Vq_V;
      _InversePark_.fSinAng = State_._SinTheta_unitless;
      _InversePark_.fCosAng = State_._CosTheta_unitless;
      _InversePark_.m_dq2albe(&_InversePark_);
      
      State_._Valpha_V = _InversePark_.fAl;
      State_._Vbeta_V = _InversePark_.fBe;
      
      // Populate output for external modulation
      Output._Valpha_V = State_._Valpha_V;
      Output._Vbeta_V = State_._Vbeta_V;
      Output._Vdc_V = _Config_._DcBusVoltage_V;
      Output._ElectricalAngle_Rad = State_._ElectricalAngle_Rad;
      Output._VoltageLimited = State_._DcBusCurrentLimited;  // Voltage saturation flag
      
      return Output;
  }