/**
***********************************************************************************
* @file    NPulse.h
* @date    2026-02-18
* @brief   Synchronous N-Pulse Modulation Scheme.
* Locks switching frequency to an integer multiple of the fundamental.
***********************************************************************************
*/

#pragma once

#include "BaseSchema.h"
#include "space_vector_transfs/vector_transfs.h"

/**
* @brief Configuration for Synchronous N-Pulse modulation.
*/
struct NPulseConfig : public ModulationCommonConfig {
    /** * @brief The integer multiple of the fundamental frequency (N).
     * Defines the number of pulses per half-cycle (if N is odd).
     * Recommended to be an odd multiple of 3 (e.g., 9, 15, 21, 33) for 3-phase symmetry.
     */
    int PulseRatio_ = 21; 

    /**
     * @brief Minimum safety frequency for the carrier. 
     * Prevents the carrier from dropping to unsafe levels at low speeds.
     */
    float MinCarrier_Hz_ = 200.0f;
};

/**
* @brief Implementation of Synchronous N-Pulse Modulation.
*/
class NPulseModulationScheme : public ModulationScheme {
    public:
    NPulseModulationScheme() = default;

    /**
    * @brief Applies specific N-Pulse configuration.
    */
    void ApplyConfig(NPulseConfig _Config);

    /**
    * @brief Synchronous schemes rely on strict phase alignment. 
    * Interpolating (blending) this with other schemes causes beat frequencies and glitches.
    * Therefore, we Require Hard Transitions.
    */
    bool RequiresHardTransition() override { return true; }

    /**
    * @brief Updates the modulation output based on FOC/Control input.
    * Calculates carrier frequency as strict multiple of fundamental.
    */
    HardwareCommand Update(ModulationInput _Input, float _Weight_unitless) override;

    private:
    NPulseConfig SpecificConfig_;
};