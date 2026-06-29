/**
***********************************************************************************
* @file    SPWM.h
* @date    2026-02-18
* @brief   Sinusoidal PWM Scheme implementation with Carrier Frequency Ramping.
***********************************************************************************
*/

#pragma once

#include "BaseSchema.h"
#include "space_vector_transfs/vector_transfs.h"

/**
* @brief Configuration for SPWM modulation.
* Inherits from ModulationCommonConfig to handle Influence zones and Max Modulation.
*/
struct SPWMConfig : public ModulationCommonConfig {
    float CarrierStart_Hz_ = 2000.0f; 
    float CarrierEnd_Hz_   = 2000.0f; 
};

/**
* @brief Implementation of Sinusoidal PWM (SPWM).
*/
class SPWMModulationScheme : public ModulationScheme {
    public:
    SPWMModulationScheme() = default;

    /**
    * @brief Applies specific SPWM configuration.
    * @param _Config The configuration object containing carrier and base settings.
    */
    void ApplyConfig(SPWMConfig _Config);

    /**
    * @brief Sinusoidal PWM is an asynchronous scheme and does not require hard transitions.
    */
    bool RequiresHardTransition() override { return false; }

    /**
    * @brief Updates the modulation output based on FOC/Control input.
    * @param _Input Standardized system state snapshot.
    * @param _Weight_unitless Transition scalar (ignored for this implementation).
    * @return HardwareCommand Duty cycles and Carrier Frequency.
    */
    HardwareCommand Update(ModulationInput _Input, float _Weight_unitless) override;

    private:
    SPWMConfig SpecificConfig_;

    /**
    * @brief Internal helper to calculate the current ramped carrier frequency.
    */
    float CalculateRampedCarrier(float _Frequency_Hz);
};