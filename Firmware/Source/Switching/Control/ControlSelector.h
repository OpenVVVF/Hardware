/**
***********************************************************************************
* @file    ControlSelector.h
* @date    2026-02-19
* @brief   Manager class that selects between inner control schemes (FOC, V/Hz).
* Handles seamless bumpless transfers (hard switching) between operating zones.
***********************************************************************************
*/

#pragma once

#include <vector>
#include <algorithm>
#include <cmath>

#include "Schemas/BaseSchema.h"

/**
* @brief  Selects the appropriate control strategy based on system velocity.
* Manages the strict transition between overlapping schemes.
*/
class ControlSelector {
    public:
        ControlSelector() = default;

        /**
        * @brief Registers a control scheme with the selector.
        * Schemes are automatically sorted by their Start Velocity.
        * @param _Scheme Pointer to the scheme instance (must remain valid).
        */
        void RegisterScheme(ControlScheme* _Scheme);

        /**
        * @brief  Main update function. Determines active schemes and computes output.
        * @param  _Sensors Standardized system state.
        * @param  _Cmd     Explicit drive targets (Torque, Flux, Speed).
        * @param  _dt_S    Time delta in seconds.
        * @return ModulationInput containing the final stationary frame voltages.
        */
        ModulationInput Update(const SensorData& _Sensors, const DriveCommand& _Cmd, float _dt_S);

    private:
        std::vector<ControlScheme*> Schemes_;
        ControlScheme* LastActiveScheme_ = nullptr; // Crucial for bumpless transfer

        /**
        * @brief  Handles the logic when two schemes overlap.
        * @param  _Lower Pointer to the lower-speed scheme.
        * @param  _Upper Pointer to the higher-speed scheme.
        * @param  _Velocity_RadPerSec Current electrical velocity.
        * @param  _Sensors Standardized system state.
        * @param  _Cmd Explicit drive targets.
        * @param  _dt_S Time delta in seconds.
        * @return Calculated ModulationInput from the winning scheme.
        */
        ModulationInput ProcessTransition(ControlScheme* _Lower, 
                                          ControlScheme* _Upper, 
                                          float _Velocity_RadPerSec, 
                                          const SensorData& _Sensors, 
                                          const DriveCommand& _Cmd, 
                                          float _dt_S);
};