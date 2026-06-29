/**
***********************************************************************************
* @file    ModulationSelector.h
* @date    2026-02-18
* @brief   Manager class that selects and blends modulation schemes.
* Handles seamless transitions (Interpolation) and hard switching (Gear Shifts).
***********************************************************************************
*/

#pragma once

#include <vector>
#include <algorithm>
#include <cmath>

#include "Schemas/BaseSchema.h"

/**
* @brief  Selects the appropriate modulation strategy based on system state.
* Manages the transition between overlapping schemes.
*/
class ModulationSelector {
    public:
    ModulationSelector() = default;

    /**
    * @brief Registers a modulation scheme with the selector.
    * Schemes are automatically sorted by their Start Frequency.
    * @param _Scheme Pointer to the scheme instance (must remain valid).
    */
    void RegisterScheme(ModulationScheme* _Scheme);

    /**
    * @brief  Main update function. Determines active schemes and computes output.
    * @param  _Input Standardized system state (Voltage, Speed, Angle).
    * @return HardwareCommand containing the final Duty Cycles and Carrier Frequency.
    */
    HardwareCommand Update(const ModulationInput& _Input);

    private:
    std::vector<ModulationScheme*> Schemes_;

    /**
    * @brief  Handles the logic when two schemes overlap.
    * @param  _Lower Pointer to the lower-frequency scheme.
    * @param  _Upper Pointer to the higher-frequency scheme.
    * @param  _Freq_Hz Current fundamental frequency.
    * @param  _Input System state.
    * @return Blended or switched HardwareCommand.
    */
    HardwareCommand ProcessTransition(ModulationScheme* _Lower, ModulationScheme* _Upper, float _Freq_Hz, const ModulationInput& _Input);

    /**
    * @brief  Linearly interpolates between two hardware commands.
    * @param  _CmdA Command A.
    * @param  _CmdB Command B.
    * @param  _Ratio Blend ratio (0.0 = A, 1.0 = B).
    * @return Interpolated command.
    */
    HardwareCommand BlendCommands(const HardwareCommand& _CmdA, const HardwareCommand& _CmdB, float _Ratio);
};