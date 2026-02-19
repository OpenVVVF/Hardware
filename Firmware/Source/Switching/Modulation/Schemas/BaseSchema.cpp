/**
***********************************************************************************
* @file    BaseSchema.cpp
* @date    2026-02-18
* @brief   Implementation of base modulation scheme logic.
***********************************************************************************
*/

#include "BaseSchema.h"

#include <algorithm>
#include <cmath>

void ModulationScheme::ApplyConfig(ModulationCommonConfig _Config) {
    Config_ = _Config;
}

bool ModulationScheme::IsActiveAtFrequency(float _Frequency_Hz, float _TransitionWindow_Hz) {
    // Check if we are anywhere inside the extended range [Start - Window, End + Window]
    bool AboveMin = _Frequency_Hz >= (Config_.InfluenceStart_Hz_ - _TransitionWindow_Hz);
    bool BelowMax = _Frequency_Hz <= (Config_.InfluenceEnd_Hz_ + _TransitionWindow_Hz);

    return (AboveMin && BelowMax);
}

bool ModulationScheme::RequiresHardTransition() {
    return false;
}