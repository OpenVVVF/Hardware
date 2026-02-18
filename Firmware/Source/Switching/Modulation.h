/**
  ***********************************************************************************
  * @file    Modulation.h
  * @date    2026-02-15
  * @brief   SPWM/SVM modulation functions using vector_transfs.h.
  ***********************************************************************************
  */

  #pragma once

  #include "FOC.h"
  #include "space_vector_transfs/vector_transfs.h"

  #include "SwitchingStructs.h"

  /**
   * @brief Generate SPWM duty cycles from FOC output
   * @param FocOut FOC output containing Valpha, Vbeta, Vdc
   * @param MaxModulation Max modulation index (e.g., 0.95)
   * @param Duties Output duty cycles [0.0, 1.0]
   */
inline void GenerateSpwm(const FocOutput& FocOut, float MaxModulation, PhaseVoltages& Duties)
{
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
   * @brief Placeholder for future SVM implementation
   * @param FocOut FOC output containing Valpha, Vbeta, Vdc, angle
   * @param MaxModulation Max modulation index
   * @param Duties Output duty cycles [0.0, 1.0]
   */
  inline void GenerateSvm(const FocOutput& FocOut, float MaxModulation, PhaseVoltages& Duties) {
      // TODO: Implement SVM using FocOut._Valpha_V, FocOut._Vbeta_V, FocOut._ElectricalAngle_Rad
      // For now, fall back to SPWM
      GenerateSpwm(FocOut, MaxModulation, Duties);
  }