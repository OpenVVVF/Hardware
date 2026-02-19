/**
***********************************************************************************
* @file    SVPWM.h
* @date    2026-02-18
* @brief   Space Vector PWM Scheme implementation with Carrier Frequency Ramping.
***********************************************************************************
*/

#pragma once

#include "BaseSchema.h"
#include "space_vector_transfs/vector_transfs.h"

/**
* @brief Configuration for SVPWM modulation.
*/
struct SVPWMConfig : public ModulationCommonConfig {
    float CarrierStart_Hz_ = 2000.0f; 
    float CarrierEnd_Hz_   = 2000.0f; 
};

/**
* @brief Implementation of Space Vector Pulse Width Modulation (SVPWM).
*/
class SVPWMModulationScheme : public ModulationScheme {
    public:
    SVPWMModulationScheme() = default;

    /**
    * @brief Applies specific SVM configuration.
    * @param _Config The configuration object containing carrier and base settings.
    */
    void ApplyConfig(SVPWMConfig _Config);

    /**
    * @brief SVM is an asynchronous scheme and supports interpolated transitions.
    */
    bool RequiresHardTransition() override { return false; }

    /**
    * @brief Updates the modulation output using Space Vector logic.
    * @param _Input Standardized system state snapshot.
    * @param _Weight_unitless Transition scalar (ignored).
    * @return HardwareCommand Duty cycles and Carrier Frequency.
    */
    HardwareCommand Update(ModulationInput _Input, float _Weight_unitless) override;

    private:
    SVPWMConfig SpecificConfig_;

    /**
    * @brief Internal helper to calculate the current ramped carrier frequency.
    */
    float CalculateRampedCarrier(float _Frequency_Hz);
};