/**
***********************************************************************************
* @file    VHz.h
* @date    2026-02-19
* @brief   Open-loop Volts per Hertz (V/f) Control.
* Inherits from ControlScheme. Synthesizes an artificial electrical 
* angle and applies a linear voltage ramp based on commanded speed.
***********************************************************************************
*/

#pragma once

#include "BaseSchema.h"

/**
* @brief Specific configuration for the V/f profile.
* Inherits influence bounds from ControlCommonConfig.
*/
struct VHzConfig : public ControlCommonConfig {
    float _NominalVelocity_RadPerSec = 0.0f; ///< Velocity where nominal voltage is reached
    float _NominalVoltage_V = 0.0f;          ///< Maximum operating voltage (usually motor rated V)
    float _VoltageBoost_V   = 0.0f;          ///< Stator resistance compensation at zero speed
};

/**
* @brief V/Hz scheme controller.
*/
class VHzController : public ControlScheme {
    public:
    VHzController()  = default;
    ~VHzController() = default;

    VHzController(const VHzController&) = delete;
    VHzController& operator=(const VHzController&) = delete;

    /**
    * @brief Applies specific V/Hz ramp configuration.
    * @param _Config The VHz-specific configuration struct.
    */
    void ApplyConfig(VHzConfig _Config);

    /**
    * @brief Core V/Hz loop. Integrates an artificial angle and sets voltage magnitude.
    * @param _Sensors System telemetry (Uses Vbus, ignores encoder/currents).
    * @param _Cmd Commanded target velocity (ignores Torque/Flux commands).
    * @param _dt_S Loop delta time in seconds.
    * @return ModulationInput (Stationary frame voltages and integrated angle).
    */
    ModulationInput Update(const SensorData& _Sensors, const DriveCommand& _Cmd, float _dt_S) override;
    
    /**
    * @brief Resets the internal angle integrator.
    */
    void Reset() override;

    // --- State Variables (Public for Telemetry) ---
    float _InternalAngle_Rad = 0.0f;
    float _TargetVoltageMagnitude_V = 0.0f;

    private:
    VHzConfig SpecificConfig_;
};