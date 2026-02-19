/**
 ***********************************************************************************
 * @file    VHz.cpp
 * @date    2026-02-19
 * @brief   V/Hz Control implementation.
 ***********************************************************************************
 */

 #include "VHz.h"
 #include <cmath>
 
 void VHzController::ApplyConfig(VHzConfig _Config) {
     ControlScheme::ApplyConfig(_Config); 
     SpecificConfig_ = _Config;
 }
 
 void VHzController::Reset() {
     _InternalAngle_Rad = 0.0f;
     _TargetVoltageMagnitude_V = 0.0f;
 }
 
 ModulationInput VHzController::Update(const SensorData& _Sensors, const DriveCommand& _Cmd, float _dt_S) {
     ModulationInput Output = {0};
 
     float targetElectricalSpeed = _Cmd._VelocityCmd_RadPerSec * MotorConfig_._PolePairs_unitless;
     _InternalAngle_Rad += targetElectricalSpeed * _dt_S;
 
     _InternalAngle_Rad = fmodf(_InternalAngle_Rad, 2.0f * 3.1415926535f);
     if (_InternalAngle_Rad < 0.0f) {
         _InternalAngle_Rad += 2.0f * 3.1415926535f;
     }
 
     float absSpeed = std::abs(_Cmd._VelocityCmd_RadPerSec);
     float slope = (SpecificConfig_._NominalVoltage_V - SpecificConfig_._VoltageBoost_V) / SpecificConfig_._NominalVelocity_RadPerSec;
     _TargetVoltageMagnitude_V = (absSpeed * slope) + SpecificConfig_._VoltageBoost_V;
 
     float maxSystemVoltage = MotorConfig_._DcBusVoltage_V * 0.5f * MotorConfig_._MaxModulation_unitless;
     if (_TargetVoltageMagnitude_V > maxSystemVoltage) {
         _TargetVoltageMagnitude_V = maxSystemVoltage;
     }
 
     Output.Valpha_V = _TargetVoltageMagnitude_V * cosf(_InternalAngle_Rad);
     Output.Vbeta_V  = _TargetVoltageMagnitude_V * sinf(_InternalAngle_Rad);
     Output.Vdc_V = MotorConfig_._DcBusVoltage_V;
     Output.Theta_Rad = _InternalAngle_Rad;
     Output.Omega_RadPerSec = targetElectricalSpeed; 
 
     return Output;
 }