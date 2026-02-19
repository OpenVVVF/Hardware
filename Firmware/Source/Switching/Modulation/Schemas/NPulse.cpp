/**
***********************************************************************************
* @file    NPulse.cpp
* @date    2026-02-18
* @brief   Implementation of Synchronous N-Pulse Modulation.
***********************************************************************************
*/

#include "NPulse.h"
#include <cmath>
#include <algorithm>

void NPulseModulationScheme::ApplyConfig(NPulseConfig _Config) {
    SpecificConfig_ = _Config;
    ModulationScheme::ApplyConfig(_Config);
}

HardwareCommand NPulseModulationScheme::Update(ModulationInput _Input, float _Weight_unitless) {
    HardwareCommand Cmd = {0};

    // 1. Calculate Fundamental Frequency
    // Omega is in Rad/Sec. F = Omega / 2Pi
    float FundamentalFreq_Hz = std::abs(_Input.Omega_RadPerSec) / 6.2831853f;

    // 2. Lock Carrier to Fundamental
    // f_sw = f_fund * N
    float TargetFreq = FundamentalFreq_Hz * (float)SpecificConfig_.PulseRatio_;

    // Safety Clamp: If the motor is stopped or very slow, maintain a minimum switching frequency
    // to ensure the power stage continues switching correctly.
    if (TargetFreq < SpecificConfig_.MinCarrier_Hz_) {
        Cmd.SwitchingFrequency_Hz = SpecificConfig_.MinCarrier_Hz_;
    } else {
        Cmd.SwitchingFrequency_Hz = TargetFreq;
    }

    // 3. Inverse Clarke Transform: Alpha/Beta -> A/B/C
    tIFClarke Iclarke = IF_CLARKE_DEFAULTS;
    Iclarke.fAl = _Input.Valpha_V;
    Iclarke.fBe = _Input.Vbeta_V;
    Iclarke.m_albe2abc(&Iclarke);

    const float Vdc = _Input.Vdc_V;

    // Safety check for invalid DC bus
    if (Vdc <= 1.0f) {
        Cmd.DutyPhU_unitless = 0.5f;
        Cmd.DutyPhV_unitless = 0.5f;
        Cmd.DutyPhW_unitless = 0.5f;
        return Cmd;
    }

    // 4. Vector Limiting (Linear region only for N-Pulse)
    // N-Pulse is typically used for specific harmonic elimination in the linear region.
    // Overmodulation can disrupt the specific pulse placement, so we clip strictly.
    const float Vlimit = 0.5f * Vdc * Config_.MaxModulationIndex_;

    float Va = Iclarke.fA;
    float Vb = Iclarke.fB;
    float Vc = Iclarke.fC;

    float MaxAbs = std::abs(Va);
    if (std::abs(Vb) > MaxAbs) MaxAbs = std::abs(Vb);
    if (std::abs(Vc) > MaxAbs) MaxAbs = std::abs(Vc);

    if (MaxAbs > Vlimit && MaxAbs > 1e-5f) {
        float ScaleDown = Vlimit / MaxAbs;
        Va *= ScaleDown;
        Vb *= ScaleDown;
        Vc *= ScaleDown;
    }

    // 5. Duty Cycle Generation (Center Aligned)
    // Because the Carrier Frequency is an exact integer multiple of the Fundamental,
    // and standard PWM timers use Center-Aligned mode, the pulses will naturally
    // center on the fundamental sine wave.
    Cmd.DutyPhU_unitless = 0.5f + (Va / Vdc);
    Cmd.DutyPhV_unitless = 0.5f + (Vb / Vdc);
    Cmd.DutyPhW_unitless = 0.5f + (Vc / Vdc);

    // 6. Clamp
    Cmd.DutyPhU_unitless = std::clamp(Cmd.DutyPhU_unitless, 0.0f, 1.0f);
    Cmd.DutyPhV_unitless = std::clamp(Cmd.DutyPhV_unitless, 0.0f, 1.0f);
    Cmd.DutyPhW_unitless = std::clamp(Cmd.DutyPhW_unitless, 0.0f, 1.0f);

    return Cmd;
}
