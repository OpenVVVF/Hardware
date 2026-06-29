/**
***********************************************************************************
* @file    RCFSPWM.h
* @date    2026-02-18
* @brief   Randomized Carrier Frequency SPWM (Spread Spectrum) implementation.
***********************************************************************************
*/

#pragma once

#include "BaseSchema.h"
#include "space_vector_transfs/vector_transfs.h"

/**
* @brief Configuration for Randomized Carrier Frequency SPWM.
* Introduces dithering parameters to spread acoustic noise.
*/
struct RCFSPWMConfig : public ModulationCommonConfig {
    float CarrierBase_Hz_   = 1200.0f; // The center frequency
    float DitherRange_Hz_   = 200.0f;  // The maximum spread (Base +/- Dither)
    
    float MinDiff_Hz_       = 10.0f;   // New freq must be at least this far from previous
    float MaxDiff_Hz_       = 50.0f;   // New freq must be at most this far from previous
    
    float UpdatePeriod_ms_  = 1.0f;    // How often (in time) to randomize the frequency
};

/**
* @brief Implementation of Randomized Carrier Frequency Sinusoidal PWM.
*/
class RCFSPWMModulationScheme : public ModulationScheme {
    public:
    RCFSPWMModulationScheme();

    /**
    * @brief Applies specific RCFSPWM configuration.
    */
    void ApplyConfig(RCFSPWMConfig _Config);

    /**
    * @brief RCFSPWM does not require hard transitions.
    */
    bool RequiresHardTransition() override { return false; }

    /**
    * @brief Updates the modulation output and manages frequency randomization.
    * @param _Input Standardized system state snapshot.
    * @param _Weight_unitless Transition scalar (0.0 to 1.0).
    * @return HardwareCommand Duty cycles and the current (randomized) Carrier Frequency.
    */
    HardwareCommand Update(ModulationInput _Input, float _Weight_unitless) override;

    private:
    RCFSPWMConfig SpecificConfig_;

    // State variables for randomization logic
    float CurrentCarrier_Hz_;
    float TimeSinceLastUpdate_ms_;

    /**
    * @brief Calculates the next random frequency satisfying Min/Max diff and Global bounds.
    * Scales the dither range based on the current transition weight.
    */
    void RandomizeCarrier(float _Scale);

    /**
    * @brief Helper to generate a random float between min and max.
    */
    float RandomFloat(float _Min, float _Max);
};