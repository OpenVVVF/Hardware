/**
***********************************************************************************
* @file    ControlSelector.cpp
* @date    2026-02-19
* @brief   Implementation of the generic Control Selector logic.
***********************************************************************************
*/

#include "ControlSelector.h"

void ControlSelector::RegisterScheme(ControlScheme* _Scheme) {
    Schemes_.push_back(_Scheme);

    // Keep schemes sorted by their start velocity.
    // This simplifies searching and determining "Lower" vs "Upper" schemes.
    std::sort(Schemes_.begin(), Schemes_.end(), [](ControlScheme* a, ControlScheme* b) {
        return a->Config_.InfluenceStart_RadPerSec_ < b->Config_.InfluenceStart_RadPerSec_;
    });
}

ModulationInput ControlSelector::Update(const SensorData& _Sensors, const DriveCommand& _Cmd, float _dt_S) {
    // We use absolute velocity so the selector works seamlessly in forward and reverse
    float absVelocity = std::abs(_Sensors._EncoderVelocity_RadPerSec);

    ControlScheme* Active1 = nullptr;
    ControlScheme* Active2 = nullptr;

    // 1. Identify Active Schemes
    for (auto* Scheme : Schemes_) {
        // Assume 0 transition window here because the Selector manages the blend
        // window based on the physical overlap of the two schemes.
        if (Scheme->IsActiveAtVelocity(absVelocity, 0.0f)) {
            if (!Active1) {
                Active1 = Scheme;
            } else if (!Active2) {
                Active2 = Scheme;
            } else {
                // More than 2 schemes overlap? Configuration error. Break and use first two.
                break;
            }
        }
    }

    // Case A: No valid scheme found (e.g., Velocity > Max Configured).
    if (!Active1) {
        LastActiveScheme_ = nullptr;
        return {0.0f, 0.0f, _Sensors._DcBusVoltage_V, 0.0f, 0.0f}; // Safe zero-voltage state
    }

    // Case B: Single Scheme Active (Steady State).
    if (!Active2) {
        // Bumpless Transfer check
        if (Active1 != LastActiveScheme_) {
            Active1->Reset();
            LastActiveScheme_ = Active1;
        }
        return Active1->Update(_Sensors, _Cmd, _dt_S);
    }

    // Case C: Transition Zone (Two schemes overlapping).
    return ProcessTransition(Active1, Active2, absVelocity, _Sensors, _Cmd, _dt_S);
}

ModulationInput ControlSelector::ProcessTransition(ControlScheme* _Lower, 
                                                   ControlScheme* _Upper, 
                                                   float _Velocity_RadPerSec, 
                                                   const SensorData& _Sensors, 
                                                   const DriveCommand& _Cmd, 
                                                   float _dt_S) {
    
    // 1. Determine the Overlap Region
    float OverlapStart = _Upper->Config_.InfluenceStart_RadPerSec_;
    float OverlapEnd = _Lower->Config_.InfluenceEnd_RadPerSec_;
    float OverlapWidth = OverlapEnd - OverlapStart;

    // Calculate progress through the overlap (0.0 to 1.0)
    float Progress = 0.0f;
    if (OverlapWidth > 0.001f) {
        Progress = (_Velocity_RadPerSec - OverlapStart) / OverlapWidth;
    }

    if (Progress < 0.0f) Progress = 0.0f;
    if (Progress > 1.0f) Progress = 1.0f;

    // 2. Execute Hard Switch (Gear Shift)
    // Control schemes do not support fading. We switch instantly at 50% overlap.
    ControlScheme* SelectedScheme = (Progress < 0.5f) ? _Lower : _Upper;

    // 3. Bumpless Transfer Check
    // If the active scheme just changed this cycle, we MUST reset its integrators 
    // before allowing it to calculate a voltage vector.
    if (SelectedScheme != LastActiveScheme_) {
        SelectedScheme->Reset();
        LastActiveScheme_ = SelectedScheme;
    }

    // 4. Update the winning scheme
    return SelectedScheme->Update(_Sensors, _Cmd, _dt_S);
}