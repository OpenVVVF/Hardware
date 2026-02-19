/**
***********************************************************************************
* @file    SVPWM.cpp
* @date    2026-02-18
* @brief   Implementation of Space Vector PWM with Zero-Sequence Injection.
***********************************************************************************
*/

#include "SVPWM.h"
#include <cmath>
#include <algorithm>

void SvmModulationScheme::ApplyConfig(SvmConfig _Config) {
    SpecificConfig_ = _Config;
    ModulationScheme::ApplyConfig(_Config);
}



HardwareCommand SvmModulationScheme::Update(ModulationInput _Input, float _Weight_unitless) {
    HardwareCommand Cmd = {0};

    // 1. Calculate Target Carrier Frequency via the frequency ramp
    float FundamentalFreq_Hz = std::abs(_Input.Omega_RadPerSec) / 6.2831853f;
    Cmd.SwitchingFrequency_Hz = CalculateRampedCarrier(FundamentalFreq_Hz);

    const float Vdc = _Input.Vdc_V;
    
    // Safety check for invalid DC bus voltage
    if (Vdc <= 1.0f) {
        Cmd.DutyPhU_unitless = 0.5f;
        Cmd.DutyPhV_unitless = 0.5f;
        Cmd.DutyPhW_unitless = 0.5f;
        return Cmd;
    }

    // 2. Normalization and Magnitude Limiting
    // SVPWM linear modulation range limit is Vdc / sqrt(3)
    const float VmaxLinear = (Vdc / 1.7320508f) * Config_.MaxModulationIndex_;

    float Valpha = _Input.Valpha_V;
    float Vbeta = _Input.Vbeta_V;

    // Calculate vector magnitude to enforce linear region limits
    float Vmag = std::sqrt(Valpha * Valpha + Vbeta * Vbeta);

    if (Vmag > VmaxLinear && Vmag > 1e-6f) {
        float Scale = VmaxLinear / Vmag;
        Valpha *= Scale;
        Vbeta *= Scale;
    }

    // 3. Inverse Clarke Transform: Alpha/Beta -> A/B/C
    tIFClarke Iclarke = IF_CLARKE_DEFAULTS;
    Iclarke.fAl = Valpha;
    Iclarke.fBe = Vbeta;
    Iclarke.m_albe2abc(&Iclarke);

    float Va = Iclarke.fA;
    float Vb = Iclarke.fB;
    float Vc = Iclarke.fC;

    // 4. SVPWM: Zero Sequence Injection (Centered)
    // Calculate Common Mode Voltage: Vcom = (Vmax + Vmin) / 2
    float Vmax = std::max({Va, Vb, Vc});
    float Vmin = std::min({Va, Vb, Vc});
    float Vcom = (Vmax + Vmin) * 0.5f;

    // 5. Duty Cycle Calculation
    // Subtract common mode voltage to center the switching waveforms
    Cmd.DutyPhU_unitless = 0.5f + (Va - Vcom) / Vdc;
    Cmd.DutyPhV_unitless = 0.5f + (Vb - Vcom) / Vdc;
    Cmd.DutyPhW_unitless = 0.5f + (Vc - Vcom) / Vdc;

    // 6. Safety Clamp to PWM timer limits [0.0, 1.0]
    Cmd.DutyPhU_unitless = std::clamp(Cmd.DutyPhU_unitless, 0.0f, 1.0f);
    Cmd.DutyPhV_unitless = std::clamp(Cmd.DutyPhV_unitless, 0.0f, 1.0f);
    Cmd.DutyPhW_unitless = std::clamp(Cmd.DutyPhW_unitless, 0.0f, 1.0f);

    return Cmd;
}

float SvmModulationScheme::CalculateRampedCarrier(float _Frequency_Hz) {
    if (std::abs(SpecificConfig_.CarrierStart_Hz_ - SpecificConfig_.CarrierEnd_Hz_) < 0.1f) {
        return SpecificConfig_.CarrierStart_Hz_;
    }

    float Range = Config_.InfluenceEnd_Hz_ - Config_.InfluenceStart_Hz_;
    if (Range <= 0.1f) return SpecificConfig_.CarrierStart_Hz_;

    float Progress = (_Frequency_Hz - Config_.InfluenceStart_Hz_) / Range;
    Progress = std::clamp(Progress, 0.0f, 1.0f);

    float DeltaCarrier = SpecificConfig_.CarrierEnd_Hz_ - SpecificConfig_.CarrierStart_Hz_;
    return SpecificConfig_.CarrierStart_Hz_ + (Progress * DeltaCarrier);
}