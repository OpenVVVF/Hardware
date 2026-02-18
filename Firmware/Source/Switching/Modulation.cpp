/**
 ***********************************************************************************
 * @file    Modulation.cpp
 * @date    2026-02-15
 * @brief   SPWM/SVM modulation functions using vector_transfs.h.
 ***********************************************************************************
 */

#include "Modulation.h"

#include "space_vector_transfs/vector_transfs.h"

/**
 * @brief Generate SPWM duty cycles from FOC output
 * @param FocOut FOC output containing Valpha, Vbeta, Vdc
 * @param MaxModulation Max modulation index (e.g., 0.95)
 * @param Duties Output duty cycles [0.0, 1.0]
 */
void GenerateSpwm(const FocOutput& FocOut, float MaxModulation, PhaseVoltages& Duties) {
    tIFClarke Iclarke = IF_CLARKE_DEFAULTS;

    Iclarke.fAl = FocOut._Valpha_V;
    Iclarke.fBe = FocOut._Vbeta_V;
    Iclarke.m_albe2abc(&Iclarke);

    const float Vdc = FocOut._Vdc_V;
    if (Vdc <= 1e-6f) {
        Duties._Du_unitless = 0.5f;
        Duties._Dv_unitless = 0.5f;
        Duties._Dw_unitless = 0.5f;
        return;
    }

    // Limit phase refs to what the bus can realize (SPWM-ish)
    const float Vlimit = 0.5f * Vdc * MaxModulation;

    float Va = Iclarke.fA;
    float Vb = Iclarke.fB;
    float Vc = Iclarke.fC;

    float maxAbs = fabsf(Va);
    if (fabsf(Vb) > maxAbs) maxAbs = fabsf(Vb);
    if (fabsf(Vc) > maxAbs) maxAbs = fabsf(Vc);

    float scaleDown = 1.0f;
    if (maxAbs > Vlimit && maxAbs > 1e-9f) {
        scaleDown = Vlimit / maxAbs;
        Va *= scaleDown;
        Vb *= scaleDown;
        Vc *= scaleDown;
    }

    // Convert phase voltage ref to centered duty
    Duties._Du_unitless = 0.5f + (Va / Vdc);
    Duties._Dv_unitless = 0.5f + (Vb / Vdc);
    Duties._Dw_unitless = 0.5f + (Vc / Vdc);

    // Safety clamp
    if (Duties._Du_unitless < 0.0f) Duties._Du_unitless = 0.0f;
    if (Duties._Du_unitless > 1.0f) Duties._Du_unitless = 1.0f;
    if (Duties._Dv_unitless < 0.0f) Duties._Dv_unitless = 0.0f;
    if (Duties._Dv_unitless > 1.0f) Duties._Dv_unitless = 1.0f;
    if (Duties._Dw_unitless < 0.0f) Duties._Dw_unitless = 0.0f;
    if (Duties._Dw_unitless > 1.0f) Duties._Dw_unitless = 1.0f;
}

/**
 * @brief Generate SVPWM duty cycles from FOC output
 * @param FocOut FOC output containing Valpha, Vbeta, Vdc
 * @param MaxModulation Max modulation index (e.g., 0.95 relative to linear SVPWM limit)
 * @param Duties Output duty cycles [0.0, 1.0]
 */
void GenerateSvm(const FocOutput& FocOut, float MaxModulation, PhaseVoltages& Duties) {
    const float Vdc = FocOut._Vdc_V;

    // Safety: Check for valid DC link voltage
    if (Vdc <= 1e-6f) {
        Duties._Du_unitless = 0.5f;
        Duties._Dv_unitless = 0.5f;
        Duties._Dw_unitless = 0.5f;
        return;
    }

    // 1. Normalization and Magnitude Limiting
    // SVPWM linear modulation range limit is Vdc / sqrt(3)
    const float VmaxLinear = (Vdc / sqrtf(3.0f)) * MaxModulation;

    float Valpha = FocOut._Valpha_V;
    float Vbeta = FocOut._Vbeta_V;

    // Calculate vector magnitude to enforce linear region limits
    float Vmag = sqrtf(Valpha * Valpha + Vbeta * Vbeta);

    if (Vmag > VmaxLinear && Vmag > 1e-9f) {
        float scale = VmaxLinear / Vmag;
        Valpha *= scale;
        Vbeta *= scale;
    }

    // 2. Inverse Clarke Transform (Alpha-Beta to ABC)
    // Using the provided library structure
    tIFClarke Iclarke = IF_CLARKE_DEFAULTS;
    Iclarke.fAl = Valpha;
    Iclarke.fBe = Vbeta;
    Iclarke.m_albe2abc(&Iclarke);

    float Va = Iclarke.fA;
    float Vb = Iclarke.fB;
    float Vc = Iclarke.fC;

    // 3. SVPWM: Zero Sequence Injection (Centered)
    // Calculate Common Mode Voltage: Vcom = (Vmax + Vmin) / 2
    float Vmax = Va;
    float Vmin = Va;

    if (Vb > Vmax) Vmax = Vb;
    if (Vc > Vmax) Vmax = Vc;

    if (Vb < Vmin) Vmin = Vb;
    if (Vc < Vmin) Vmin = Vc;

    float Vcom = (Vmax + Vmin) * 0.5f;

    // 4. Duty Cycle Calculation
    // Subtract common mode voltage to center the switching waveforms
    Duties._Du_unitless = 0.5f + (Va - Vcom) / Vdc;
    Duties._Dv_unitless = 0.5f + (Vb - Vcom) / Vdc;
    Duties._Dw_unitless = 0.5f + (Vc - Vcom) / Vdc;

    // 5. Safety Clamp
    // Ensure duties stay within PWM timer limits [0.0, 1.0]
    if (Duties._Du_unitless < 0.0f) Duties._Du_unitless = 0.0f;
    if (Duties._Du_unitless > 1.0f) Duties._Du_unitless = 1.0f;
    if (Duties._Dv_unitless < 0.0f) Duties._Dv_unitless = 0.0f;
    if (Duties._Dv_unitless > 1.0f) Duties._Dv_unitless = 1.0f;
    if (Duties._Dw_unitless < 0.0f) Duties._Dw_unitless = 0.0f;
    if (Duties._Dw_unitless > 1.0f) Duties._Dw_unitless = 1.0f;
}