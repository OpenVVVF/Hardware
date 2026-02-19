/**
 ***********************************************************************************
 * @file    VHz.h
 * @date    2026-02-19
 * @brief   Open-loop Volts per Hertz (V/f) Control.
 ***********************************************************************************
 */

 #pragma once

 #include "BaseSchema.h"
 
 struct VHzConfig : public ControlCommonConfig {
     float _NominalVelocity_RadPerSec = 0.0f; 
     float _NominalVoltage_V = 0.0f;          
     float _VoltageBoost_V = 0.0f;            
 };
 
 class VHzController : public ControlScheme {
    public:
     VHzController() = default;
     ~VHzController() = default;
 
     VHzController(const VHzController&) = delete;
     VHzController& operator=(const VHzController&) = delete;
 
     void ApplyConfig(VHzConfig _Config);
 
     ModulationInput Update(const SensorData& _Sensors, const DriveCommand& _Cmd, float _dt_S) override;
     void Reset() override;
 
     float _InternalAngle_Rad = 0.0f;
     float _TargetVoltageMagnitude_V = 0.0f;
 
    private:
     VHzConfig SpecificConfig_;
 };