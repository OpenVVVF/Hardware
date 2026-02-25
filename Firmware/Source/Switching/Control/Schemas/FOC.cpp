/**
***********************************************************************************
* @file    FOC.cpp
* @date    2026-02-19
* @brief   Field-Oriented Control implementation.
***********************************************************************************
*/

#include "FOC.h"
#include <cmath>

FocController::FocController()
    : _ElectricalAngle_Rad(0.0f), _ElectricalSpeed_RadPerSec(0.0f),
    _SinTheta_unitless(0.0f), _CosTheta_unitless(1.0f),
    _Ialpha_A(0.0f), _Ibeta_A(0.0f), _Id_A(0.0f), _Iq_A(0.0f),
    _IdCommanded_A(0.0f), _IqCommanded_A(0.0f), 
    _Vd_V(0.0f), _Vq_V(0.0f), _Valpha_V(0.0f), _Vbeta_V(0.0f), 
    _PhaseCurrentLimited(false), _DcBusCurrentLimited(false), 
    _VdDecouplingFF_V_(0.0f), _VqDecouplingFF_V_(0.0f) {

    _Clarke_      = {}; _Clarke_.m_abc2albe = tFFClarke_abc2albe;
    _Park_        = {}; _Park_.m_albe2dq    = tFPark_albe2dq;
    _InversePark_ = {}; _InversePark_.m_dq2albe = tIPark_dq2albe;
    
    Sensors_      = {};
    _CurrentLoop  = {};
}

void FocController::ApplyConfig(FocConfig _Config) {
    ControlScheme::ApplyConfig(_Config);
    SpecificConfig_ = _Config;
    
    _CurrentLoop.Kp_ = SpecificConfig_._Kp_Q_axis; 
    _CurrentLoop.Ki_ = SpecificConfig_._Ki_Q_axis; 
}

void FocController::Reset() {
    _CurrentLoop.Reset();
    
    _ElectricalAngle_Rad = 0.0f; _ElectricalSpeed_RadPerSec = 0.0f;
    _SinTheta_unitless   = 0.0f; _CosTheta_unitless   = 1.0f;
    _Ialpha_A            = 0.0f; _Ibeta_A             = 0.0f; 
    _Id_A                = 0.0f; _Iq_A                = 0.0f;
    _IdCommanded_A       = 0.0f; _IqCommanded_A       = 0.0f;
    _Vd_V                = 0.0f; _Vq_V                = 0.0f; 
    _Valpha_V            = 0.0f; _Vbeta_V             = 0.0f;
    
    _PhaseCurrentLimited  = false; 
    _DcBusCurrentLimited  = false;
    _VdDecouplingFF_V_    = 0.0f; 
    _VqDecouplingFF_V_    = 0.0f;
}

void FocController::CalculateDecoupling() {
    // _VdDecouplingFF_V_ = -_ElectricalSpeed_RadPerSec * MotorConfig_._Lq_Henry * _Iq_A;
    // _VqDecouplingFF_V_ = (_ElectricalSpeed_RadPerSec * MotorConfig_._Ld_Henry * _Id_A) + 
    //                     (_ElectricalSpeed_RadPerSec * MotorConfig_._FluxLinkage_Wb);

    // A. Inductive Cross-Coupling Decoupling (Use COMMANDED currents to prevent high-speed positive feedback)
    _VdDecouplingFF_V_ = -_ElectricalSpeed_RadPerSec * MotorConfig_._Lq_Henry * _IqCommanded_A;
    
    // B. Back-EMF and D-Axis Decoupling
    _VqDecouplingFF_V_ = (_ElectricalSpeed_RadPerSec * MotorConfig_._Ld_Henry * _IdCommanded_A) + 
                         (_ElectricalSpeed_RadPerSec * MotorConfig_._FluxLinkage_Wb);

}

ModulationInput FocController::Update(const SensorData& _Sensors, const DriveCommand& _Cmd, float _dt_S) {
    ModulationInput Output = {0};
    Sensors_ = _Sensors;

    // --- 1. DYNAMIC LIMIT RESOLUTION ---
    if (SpecificConfig_._SoftVoltageLimit_V > 0.001f) {
        _CurrentLoop.MaxVoltageLimit_ = SpecificConfig_._SoftVoltageLimit_V;
    } else {
        _CurrentLoop.MaxVoltageLimit_ = _Sensors._DcBusVoltage_V * 0.5f * MotorConfig_._MaxModulation_unitless;
    }

    // --- 2. SENSOR PROCESSING & TRANSFORMS ---
    _ElectricalAngle_Rad = fmodf(Sensors_._EncoderPosition_Rad * MotorConfig_._PolePairs_unitless + _EncoderOffset_Rad, 2.0f * 3.1415926535f);
    if (_ElectricalAngle_Rad < 0.0f) {
        _ElectricalAngle_Rad += 2.0f * 3.1415926535f;
    }

    _ElectricalSpeed_RadPerSec = Sensors_._EncoderVelocity_RadPerSec * MotorConfig_._PolePairs_unitless;
    _SinTheta_unitless = sinf(_ElectricalAngle_Rad);
    _CosTheta_unitless = cosf(_ElectricalAngle_Rad);

    // Forward Clarke (ABC -> Alpha/Beta)
    _Clarke_.fA = Sensors_._Iu_A; 
    _Clarke_.fB = Sensors_._Iv_A; 
    _Clarke_.fC = Sensors_._Iw_A;
    _Clarke_.m_abc2albe(&_Clarke_);
    
    _Ialpha_A = _Clarke_.fAl; i_alpha = _Ialpha_A;
    _Ibeta_A  = _Clarke_.fBe; i_beta  = _Ibeta_A;

    // Forward Park (Alpha/Beta -> D/Q)
    _Park_.fAl = _Ialpha_A; 
    _Park_.fBe = _Ibeta_A;
    _Park_.fSinAng = _SinTheta_unitless; 
    _Park_.fCosAng = _CosTheta_unitless;
    _Park_.m_albe2dq(&_Park_);
    
    _Id_A = _Park_.fD; i_d = _Id_A;
    _Iq_A = _Park_.fQ; i_q = _Iq_A;

    // --- 3. COMMAND SATURATION ---
    _IdCommanded_A = _Cmd._IdCmd_A;
    float IqMax = MotorConfig_._SoftMaxPhaseCurrent_A;
    float IqMin = -MotorConfig_._SoftMaxPhaseCurrent_A;
    
    _IqCommanded_A = fmaxf(IqMin, fminf(IqMax, _Cmd._IqCmd_A));
    _PhaseCurrentLimited = (_IqCommanded_A != _Cmd._IqCmd_A);

    // --- 4. VECTOR PI CONTROL ---
    CalculateDecoupling();

    float TotalVd_FF = _VdDecouplingFF_V_ + _Cmd._VdFeedforward_V;
    float TotalVq_FF = _VqDecouplingFF_V_ + _Cmd._VqFeedforward_V;
    
    float Id_err = _IdCommanded_A - _Id_A;
    float Iq_err = _IqCommanded_A - _Iq_A;

    _CurrentLoop.Update(Id_err, Iq_err, TotalVd_FF, TotalVq_FF, _dt_S, _Vd_V, _Vq_V);

    // Diagnostic clipping check
    float v_mag = sqrtf(_Vd_V * _Vd_V + _Vq_V * _Vq_V);
    _DcBusCurrentLimited = (v_mag >= _CurrentLoop.MaxVoltageLimit_ * 0.99f);

    // --- 5. INVERSE PARK TRANSFORM (D/Q -> Alpha/Beta) ---
    _InversePark_.fD = _Vd_V; 
    _InversePark_.fQ = _Vq_V;
    _InversePark_.fSinAng = _SinTheta_unitless;  
    _InversePark_.fCosAng = _CosTheta_unitless;
    _InversePark_.m_dq2albe(&_InversePark_);
    
    _Valpha_V = _InversePark_.fAl; 
    _Vbeta_V  = _InversePark_.fBe;

    // --- 6. OUTPUT POPULATION ---
    Output.Valpha_V = -_Valpha_V; // Inverting to match hardware modulation sign convention
    Output.Vbeta_V  = -_Vbeta_V;
    Output.Vdc_V    = _Sensors._DcBusVoltage_V;
    Output.Theta_Rad       = _ElectricalAngle_Rad;
    Output.Omega_RadPerSec = _ElectricalSpeed_RadPerSec;

    return Output;
}