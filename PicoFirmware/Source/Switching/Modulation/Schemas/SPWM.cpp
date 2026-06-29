/**
***********************************************************************************
* @file    SPWM.cpp
* @date    2026-02-18
* @brief   Implementation of Sinusoidal PWM with Carrier Frequency Ramping.
***********************************************************************************
*/

#include "SPWM.h"
#include <cmath>
#include <algorithm>

void SPWMModulationScheme::ApplyConfig(SPWMConfig _Config) {
    // Store locally for specific parameters (Carrier Ramping)
    SpecificConfig_ = _Config;

    // Apply base parameters (Influence ranges, Max Modulation) to the parent
    ModulationScheme::ApplyConfig(_Config);
}

HardwareCommand SPWMModulationScheme::Update(ModulationInput _Input, float _Weight_unitless) {
    HardwareCommand Cmd = {0};

    // 1. Calculate Target Carrier Frequency via the frequency ramp
    float FundamentalFreq_Hz = std::abs(_Input.Omega_RadPerSec) / 6.2831853f;
    Cmd.SwitchingFrequency_Hz = CalculateRampedCarrier(FundamentalFreq_Hz);

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
    // The max linear phase voltage for SPWM is Vdc/2, scaled by our config limit.
    const float Vlimit = 0.5f * Vdc * Config_.MaxModulationIndex_;

    float Va = Iclarke.fA;
    float Vb = Iclarke.fB;
    float Vc = Iclarke.fC;

    float MaxAbs = std::abs(Va);
    if (std::abs(Vb) > MaxAbs) MaxAbs = std::abs(Vb);
    if (std::abs(Vc) > MaxAbs) MaxAbs = std::abs(Vc);

    // If magnitude exceeds the realizable voltage, scale the vector down proportionally
    if (MaxAbs > Vlimit && MaxAbs > 1e-5f) {
        float ScaleDown = Vlimit / MaxAbs;
        Va *= ScaleDown;
        Vb *= ScaleDown;
        Vc *= ScaleDown;
    }

    // 4. Convert Voltage to Duty Cycle (Centered)
    // Duty = 0.5 + (Vphase / Vdc)
    Cmd.DutyPhU_unitless = 0.5f + (Va / Vdc);
    Cmd.DutyPhV_unitless = 0.5f + (Vb / Vdc);
    Cmd.DutyPhW_unitless = 0.5f + (Vc / Vdc);

    // 5. Final Protection Clamp
    Cmd.DutyPhU_unitless = std::clamp(Cmd.DutyPhU_unitless, 0.0f, 1.0f);
    Cmd.DutyPhV_unitless = std::clamp(Cmd.DutyPhV_unitless, 0.0f, 1.0f);
    Cmd.DutyPhW_unitless = std::clamp(Cmd.DutyPhW_unitless, 0.0f, 1.0f);

    return Cmd;
}

float SPWMModulationScheme::CalculateRampedCarrier(float _Frequency_Hz) {
    // If Start and End are effectively identical, return early
    if (std::abs(SpecificConfig_.CarrierStart_Hz_ - SpecificConfig_.CarrierEnd_Hz_) < 0.1f) {
        return SpecificConfig_.CarrierStart_Hz_;
    }

    // Determine the width of the frequency range where this scheme operates
    float Range = Config_.InfluenceEnd_Hz_ - Config_.InfluenceStart_Hz_;
    if (Range <= 0.1f) {
        return SpecificConfig_.CarrierStart_Hz_;
    }

    // Calculate progress (0.0 to 1.0) through the influence zone
    float Progress = (_Frequency_Hz - Config_.InfluenceStart_Hz_) / Range;
    Progress = std::clamp(Progress, 0.0f, 1.0f);

    // Linearly interpolate the carrier frequency
    float DeltaCarrier = SpecificConfig_.CarrierEnd_Hz_ - SpecificConfig_.CarrierStart_Hz_;
    return SpecificConfig_.CarrierStart_Hz_ + (Progress * DeltaCarrier);
}