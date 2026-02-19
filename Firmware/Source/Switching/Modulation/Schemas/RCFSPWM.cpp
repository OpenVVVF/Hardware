/**
***********************************************************************************
* @file    RCFSPWM.cpp
* @date    2026-02-18
* @brief   Implementation of Randomized Carrier Frequency SPWM.
***********************************************************************************
*/

#include "RCFSPWM.h"
#include <cmath>
#include <algorithm>
#include <cstdlib> 

RCFSPWMModulationScheme::RCFSPWMModulationScheme() 
    : CurrentCarrier_Hz_(10000.0f)
    , TimeSinceLastUpdate_ms_(0.0f) 
{
}

void RCFSPWMModulationScheme::ApplyConfig(RCFSPWMConfig _Config) {
    SpecificConfig_ = _Config;
    
    // Initialize carrier to base if strictly invalid
    if (CurrentCarrier_Hz_ < 0.1f) {
        CurrentCarrier_Hz_ = SpecificConfig_.CarrierBase_Hz_;
    }

    // Apply base parameters to the parent class
    ModulationScheme::ApplyConfig(_Config);
}

HardwareCommand RCFSPWMModulationScheme::Update(ModulationInput _Input, float _Weight_unitless) {
    HardwareCommand Cmd = {0};

    // 1. Dither Management & Fading Logic
    // Scale the allowable deviation based on the transition weight.
    // As weight approaches 0, the carrier is forced back to Base.
    float EffectiveRange = SpecificConfig_.DitherRange_Hz_ * _Weight_unitless;
    float BaseFreq = SpecificConfig_.CarrierBase_Hz_;

    // Clamp current carrier immediately to the scaled range (Hysteresis correction)
    float GlobalMin = BaseFreq - EffectiveRange;
    float GlobalMax = BaseFreq + EffectiveRange;
    CurrentCarrier_Hz_ = std::clamp(CurrentCarrier_Hz_, GlobalMin, GlobalMax);

    // Track time accumulation based on the switching period
    float Period_sec = 1.0f / CurrentCarrier_Hz_;
    TimeSinceLastUpdate_ms_ += (Period_sec * 1000.0f);

    if (TimeSinceLastUpdate_ms_ >= SpecificConfig_.UpdatePeriod_ms_) {
        RandomizeCarrier(_Weight_unitless);
        TimeSinceLastUpdate_ms_ = 0.0f;
    }

    Cmd.SwitchingFrequency_Hz = CurrentCarrier_Hz_;

    // 2. Perform Inverse Clarke Transform: Alpha/Beta -> A/B/C
    tIFClarke Iclarke = IF_CLARKE_DEFAULTS;
    Iclarke.fAl = _Input.Valpha_V;
    Iclarke.fBe = _Input.Vbeta_V;
    Iclarke.m_albe2abc(&Iclarke);

    const float Vdc = _Input.Vdc_V;
    
    // Safety check for invalid DC bus voltage
    if (Vdc <= 1.0f) {
        Cmd.DutyPhU_unitless = 0.5f;
        Cmd.DutyPhV_unitless = 0.5f;
        Cmd.DutyPhW_unitless = 0.5f;
        return Cmd;
    }

    // 3. Apply Modulation Limits (Vector Clipping)
    const float Vlimit = 0.5f * Vdc * Config_.MaxModulationIndex_;

    float Va = Iclarke.fA;
    float Vb = Iclarke.fB;
    float Vc = Iclarke.fC;

    float MaxAbs = std::max({std::abs(Va), std::abs(Vb), std::abs(Vc)});

    // If magnitude exceeds the realizable voltage, scale the vector down proportionally
    if (MaxAbs > Vlimit && MaxAbs > 1e-5f) {
        float ScaleDown = Vlimit / MaxAbs;
        Va *= ScaleDown;
        Vb *= ScaleDown;
        Vc *= ScaleDown;
    }

    // 4. Convert Voltage to Duty Cycle (Centered)
    Cmd.DutyPhU_unitless = 0.5f + (Va / Vdc);
    Cmd.DutyPhV_unitless = 0.5f + (Vb / Vdc);
    Cmd.DutyPhW_unitless = 0.5f + (Vc / Vdc);

    // 5. Final Protection Clamp
    Cmd.DutyPhU_unitless = std::clamp(Cmd.DutyPhU_unitless, 0.0f, 1.0f);
    Cmd.DutyPhV_unitless = std::clamp(Cmd.DutyPhV_unitless, 0.0f, 1.0f);
    Cmd.DutyPhW_unitless = std::clamp(Cmd.DutyPhW_unitless, 0.0f, 1.0f);

    return Cmd;
}

void RCFSPWMModulationScheme::RandomizeCarrier(float _Scale) {
    // Scale step sizes and range by the weight
    float Range   = SpecificConfig_.DitherRange_Hz_ * _Scale;
    float MinDiff = SpecificConfig_.MinDiff_Hz_ * _Scale;
    float MaxDiff = SpecificConfig_.MaxDiff_Hz_ * _Scale;
    float Base    = SpecificConfig_.CarrierBase_Hz_;

    // If scale is negligible, snap to base
    if (_Scale < 0.01f) {
        CurrentCarrier_Hz_ = Base;
        return;
    }

    // Global Bounds
    float GlobalMin = Base - Range;
    float GlobalMax = Base + Range;

    // Relative Window Calculation
    float UpperStart = CurrentCarrier_Hz_ + MinDiff;
    float UpperEnd   = CurrentCarrier_Hz_ + MaxDiff;
    
    float LowerStart = CurrentCarrier_Hz_ - MaxDiff;
    float LowerEnd   = CurrentCarrier_Hz_ - MinDiff;

    // Intersect Relative Windows with Global Bounds
    float ValidUpperStart = std::max(GlobalMin, UpperStart);
    float ValidUpperEnd   = std::min(GlobalMax, UpperEnd);
    bool  UpperValid      = (ValidUpperStart <= ValidUpperEnd);

    float ValidLowerStart = std::max(GlobalMin, LowerStart);
    float ValidLowerEnd   = std::min(GlobalMax, LowerEnd);
    bool  LowerValid      = (ValidLowerStart <= ValidLowerEnd);

    // Decision Logic
    if (UpperValid && LowerValid) {
        if (std::rand() % 2 == 0) {
            CurrentCarrier_Hz_ = RandomFloat(ValidUpperStart, ValidUpperEnd);
        } else {
            CurrentCarrier_Hz_ = RandomFloat(ValidLowerStart, ValidLowerEnd);
        }
    } 
    else if (UpperValid) {
        CurrentCarrier_Hz_ = RandomFloat(ValidUpperStart, ValidUpperEnd);
    } 
    else if (LowerValid) {
        CurrentCarrier_Hz_ = RandomFloat(ValidLowerStart, ValidLowerEnd);
    } 
    else {
        // Fallback: Pick a random value in the scaled global range
        CurrentCarrier_Hz_ = RandomFloat(GlobalMin, GlobalMax);
    }
}

float RCFSPWMModulationScheme::RandomFloat(float _Min, float _Max) {
    if (_Max <= _Min) return _Min;
    float Random01 = static_cast<float>(std::rand()) / static_cast<float>(RAND_MAX);
    return _Min + Random01 * (_Max - _Min);
}