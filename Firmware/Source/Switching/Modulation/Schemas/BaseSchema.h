/**
 ***********************************************************************************
 * @file    ModulBaseSchemaationScheme.h
 * @date    2026-02-18
 * @brief   Abstract base class for all modulation strategies (SVM, SHE, etc).
 * Handles frequency-based activation and transition weighting.
 ***********************************************************************************
 */

#pragma once


#include "../ModulationInput.h"
#include "../../PWMDriver.h"


/**
 * @brief Common configuration shared by ALL schemes.
 */
 struct ModulationCommonConfig {
    float MaxModulationIndex_ = 0.95f; 
    
    float InfluenceStart_Hz_; // Start influence range
    float InfluenceEnd_Hz_; // End influence range
};


/**
* @brief   Abstract base class for all modulation strategies (SVM, SHE, DPWM, etc).
*          Handles frequency-based activation and transition weighting.
*
*          This class defines the interface and common functionality for all
*          modulation schemes. Each scheme operates within a configurable
*          frequency range (InfluenceStart_Hz_ to InfluenceEnd_Hz_) and generates
*          hardware commands based on modulation input parameters.
*
*          During multi-zone operation, schemes can be blended using transition
*          weighting. The GetWeightAtFrequency() function calculates influence
*          within transition windows, enabling smooth interpolation between
*          adjacent modulation zones. If interpolation is not supported,
*          RequiresHardTransition() should return true.
*/
class ModulationScheme {


    public:

        ModulationCommonConfig Config_;


        /**
        * @brief Indicates if this function supports interpolated transitions between zones.
        */
        virtual bool RequiresHardTransition();

        /**
        * @brief Function which is called when transitioning out of this modulation scheme to another schema.
        * Weight is a scalar value from 0-1 used during transitions, where it indicates the required influence on the host device.
        */
        virtual HardwareCommand Update (ModulationInput _Input, float _Weight_unitless);


        /**
        @brief Applies configuration object.
        */
        void ApplyConfig(ModulationCommonConfig _Config);

        /**
        * @brief Helper to return true/false if this function is active at this frequency
        */
        bool IsActiveAtFrequency(float _Frequency_Hz, float _TransitionWindow_Hz = 0.0f);


};
