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
      _VqFeedforward_V_(0.0f) {

    
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
    _CurrentLoop.MaxVoltageLimit = _Config_._DcBusVoltage_V * 0.5f * _Config_._MaxModulation_unitless;
}

void FocController::SetVoltageLimit(float _Voltage_V) {
    _CurrentLoop.MaxVoltageLimit = _Voltage_V;
}

MotorConfig FocController::GetMotorConfig() const {
    return _Config_;
}

void FocController::SetDaxisGains(float Kp, float Ki) {
    _CurrentLoop.Kp = Kp;
    _CurrentLoop.Ki = Ki;
}

void FocController::SetQaxisGains(float Kp, float Ki) {
    _CurrentLoop.Kp = Kp;
    _CurrentLoop.Ki = Ki;
}

void FocController::Reset() {

    _CurrentLoop.Reset();

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

void FocController::UpdateSensors(const SensorData& Sensors) {
    // Store sensor data in public state
    Sensors_ = Sensors;

    // Compute electrical angle and speed
    _ElectricalAngle_Rad = fmodf(Sensors._EncoderPosition_Rad * _Config_._PolePairs_unitless + _EncoderOffset_Rad, 2.0f * 3.1415926535);
    if (_ElectricalAngle_Rad < 0.0f)
        _ElectricalAngle_Rad += 2.0f * 3.1415926535;

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

    _VdFeedforward_V_ = -_ElectricalSpeed_RadPerSec * _Config_._Lq_Henry * _Iq_A;
    _VqFeedforward_V_ = _ElectricalSpeed_RadPerSec * _Config_._Ld_Henry * _Id_A + _ElectricalSpeed_RadPerSec * _Config_._FluxLinkage_Wb;
}


ModulationInput FocController::UpdateVoltages(float dt_S) {
    ModulationInput Output = {0};

    // 1. Calculate feedforward terms
    CalculateDecoupling();

    // 2. Calculate errors
    float Id_err = _IdCommanded_A - _Id_A ;
    float Iq_err = _IqCommanded_A - _Iq_A;

    // 3. Execute Coupled Vector PI Loop
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
    Output.Valpha_V = -_Valpha_V;
    Output.Vbeta_V = -_Vbeta_V;
    Output.Vdc_V = _Config_._DcBusVoltage_V;
    Output.Theta_Rad = _ElectricalAngle_Rad;
    Output.Omega_RadPerSec = _ElectricalSpeed_RadPerSec;

    return Output;
}