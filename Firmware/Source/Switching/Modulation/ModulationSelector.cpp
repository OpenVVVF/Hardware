/**
***********************************************************************************
* @file    ModulationSelector.cpp
* @date    2026-02-18
* @brief   Implementation of the generic Modulation Selector logic.
***********************************************************************************
*/

#include "ModulationSelector.h"

void ModulationSelector::RegisterScheme(ModulationScheme* _Scheme) {
    Schemes_.push_back(_Scheme);

    // Keep schemes sorted by their start frequency.
    // This simplifies searching and determining "Lower" vs "Upper" schemes.
    std::sort(Schemes_.begin(), Schemes_.end(), [](ModulationScheme* a, ModulationScheme* b) {
        return a->Config_.InfluenceStart_Hz_ < b->Config_.InfluenceStart_Hz_;
    });
}

HardwareCommand ModulationSelector::Update(const ModulationInput& _Input) {
    // Convert angular velocity (rad/s) to frequency (Hz)
    float Freq_Hz = std::abs(_Input.Omega_RadPerSec) / 6.2831853f;

    ModulationScheme* Active1 = nullptr;
    ModulationScheme* Active2 = nullptr;

    // 1. Identify Active Schemes
    // We scan the list to find up to two schemes that claim this frequency range.
    for (auto* Scheme : Schemes_) {
        // We assume 0 transition window here because the Selector manages the blend
        // window based on the physical overlap of the two schemes.
        if (Scheme->IsActiveAtFrequency(Freq_Hz, 0.0f)) {
            if (!Active1) {
                Active1 = Scheme;
            } else if (!Active2) {
                Active2 = Scheme;
            } else {
                // More than 2 schemes overlap? This is a configuration error.
                // We break and use the first two found (Lowest freq priority).
                break;
            }
        }
    }

    // Case A: No valid scheme found (e.g., Freq > Max Configured).
    // Return a safe "Zero" command.
    if (!Active1) {
        return {2000.0f, 0.0f, 0.0f, 0.0f}; // Safe state
    }

    // Case B: Single Scheme Active (Steady State).
    if (!Active2) {
        return Active1->Update(_Input, 1.0f);
    }

    // Case C: Transition Zone (Two schemes overlapping).
    return ProcessTransition(Active1, Active2, Freq_Hz, _Input);
}

HardwareCommand ModulationSelector::ProcessTransition(ModulationScheme* _Lower, ModulationScheme* _Upper, float _Freq_Hz, const ModulationInput& _Input) {
    // 1. Determine the Overlap Region
    // The overlap occurs from [Upper->Start] to [Lower->End].
    float OverlapStart = _Upper->Config_.InfluenceStart_Hz_;
    float OverlapEnd = _Lower->Config_.InfluenceEnd_Hz_;
    float OverlapWidth = OverlapEnd - OverlapStart;

    // Calculate progress through the overlap (0.0 to 1.0)
    float Progress = 0.0f;
    if (OverlapWidth > 0.001f) {
        Progress = (_Freq_Hz - OverlapStart) / OverlapWidth;
    }

    // Clamp progress for safety
    if (Progress < 0.0f) Progress = 0.0f;
    if (Progress > 1.0f) Progress = 1.0f;

    // 2. Check Transition Capabilities
    // If EITHER scheme requires a hard transition (e.g., SHE, N-Pulse),
    // we must perform a hard switch. We cannot interpolate a square wave with a sine wave.
    bool HardSwitchRequired = _Lower->RequiresHardTransition() || _Upper->RequiresHardTransition();

    if (HardSwitchRequired) {
        // "Gear Shift" Logic:
        // We switch instantly at 50% influence (the midpoint of the overlap).
        // (Advanced TODO: You could sync this to Input.Theta_Rad crossing zero for ultra-smooth shifts)
        if (Progress < 0.5f) {
            return _Lower->Update(_Input, 1.0f);
        } else {
            return _Upper->Update(_Input, 1.0f);
        }
    } else {
        // "Cross-Fade" Logic:
        // Both schemes allow blending (e.g., Async SVM -> Async RCFM).
        // We calculate both and blend the results.
        
        // Weight: 1.0 means fully active, 0.0 means inactive.
        float WeightLower = 1.0f - Progress;
        float WeightUpper = Progress;

        HardwareCommand CmdLower = _Lower->Update(_Input, WeightLower);
        HardwareCommand CmdUpper = _Upper->Update(_Input, WeightUpper);

        return BlendCommands(CmdLower, CmdUpper, Progress);
    }
}

HardwareCommand ModulationSelector::BlendCommands(const HardwareCommand& _CmdA, const HardwareCommand& _CmdB, float _Ratio) {
    HardwareCommand Mixed;

    // 1. Interpolate Carrier Frequency
    // This creates the "Doppler Slide" effect for frequency changes.
    Mixed.SwitchingFrequency_Hz = _CmdA.SwitchingFrequency_Hz + _Ratio * (_CmdB.SwitchingFrequency_Hz - _CmdA.SwitchingFrequency_Hz);

    // 2. Interpolate Duty Cycles
    // This smoothly morphs the voltage vector from Scheme A to Scheme B.
    Mixed.DutyPhU_unitless = _CmdA.DutyPhU_unitless + _Ratio * (_CmdB.DutyPhU_unitless - _CmdA.DutyPhU_unitless);
    Mixed.DutyPhV_unitless = _CmdA.DutyPhV_unitless + _Ratio * (_CmdB.DutyPhV_unitless - _CmdA.DutyPhV_unitless);
    Mixed.DutyPhW_unitless = _CmdA.DutyPhW_unitless + _Ratio * (_CmdB.DutyPhW_unitless - _CmdA.DutyPhW_unitless);

    return Mixed;
}